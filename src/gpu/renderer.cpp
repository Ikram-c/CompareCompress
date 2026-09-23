/**
 * @file renderer.cpp
 * @brief Shader set-up, texture uploads and the per-item draw.
 */
#include "gpu/renderer.h"

#include "gpu/shaders.h"
#include "util/contract.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace cc {

Renderer* Renderer::instance_ = nullptr;

namespace {

/** @brief Size of the shader info-log buffer. */
constexpr int kLogBytes = 4096;
/** @brief Stale GL errors drained before and after set-up. */
constexpr int kMaxDrainedErrors = 8;
/** @brief Fallback when the driver reports no texture limit. */
constexpr int kDefaultMaxTexture = 2048;
/** @brief Texture units used by the compare shader. */
constexpr gl::GLint kUnitBefore = 0;
constexpr gl::GLint kUnitAfter = 1;
constexpr gl::GLint kUnitHeat = 2;
constexpr gl::GLint kUnitCmap = 3;
/** @brief Colour of the 1x1 texture bound when a slot has no image. */
constexpr uint8_t kBlankGrey = 40;
/** @brief Vertices of the unit quad drawn as a triangle strip. */
constexpr int kQuadVertices = 4;
/** @brief Byte alignment of the RGB8 colour-map rows / RGBA8 image rows. */
constexpr gl::GLint kTightAlignment = 1;
constexpr gl::GLint kDefaultAlignment = 4;

/**
 * @brief Compiles one shader stage.
 * @return The shader, or 0 with `err` set.
 */
gl::GLuint compile(gl::GLenum type, const char* src, std::string& err)
{
  const gl::GLuint s = gl::CreateShader(type);
  gl::ShaderSource(s, 1, &src, nullptr);
  gl::CompileShader(s);
  gl::GLint ok = 0;
  gl::GetShaderiv(s, gl::COMPILE_STATUS, &ok);
  if(ok == 0)
  {
    char log[kLogBytes] = {0};
    gl::GetShaderInfoLog(s, kLogBytes, nullptr, log);
    err = std::string(type == gl::VERTEX_SHADER ? "vertex" : "fragment") + " shader: " + log;
    gl::DeleteShader(s);
    return 0;
  }
  return s;
}

/** @brief Sets the min/mag filter of a texture. */
void set_filter(gl::GLuint tex, bool nearest)
{
  gl::BindTexture(gl::TEXTURE_2D, tex);
  gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, nearest ? gl::NEAREST : gl::LINEAR);
  gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, nearest ? gl::NEAREST : gl::LINEAR);
}

/** @brief Clamp-to-edge, linear filtering: the settings every uploaded texture uses. */
void set_default_sampling()
{
  gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
  gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
  gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
  gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
}

/**
 * @brief Pops pending GL errors.
 * @return true when none was pending.
 */
bool drain_errors()
{
  bool clean = true;
  for(int i = 0; i < kMaxDrainedErrors; ++i)
  {
    if(gl::GetError() == gl::NO_ERROR_)
    {
      break;
    }
    clean = false;
  }
  return clean;
}

/** @brief Uploads a 2D texture, reusing the storage when the size is unchanged. */
void upload_2d(GpuTexture& t, int w, int h, gl::GLint internal_format, gl::GLenum format, gl::GLenum type, const void* pixels)
{
  if(t.id == 0)
  {
    gl::GenTextures(1, &t.id);
  }
  gl::BindTexture(gl::TEXTURE_2D, t.id);
  gl::PixelStorei(gl::UNPACK_ALIGNMENT, kDefaultAlignment);
  if(t.w == w && t.h == h)
  {
    gl::TexSubImage2D(gl::TEXTURE_2D, 0, 0, 0, w, h, format, type, pixels);
  }
  else
  {
    gl::TexImage2D(gl::TEXTURE_2D, 0, internal_format, w, h, 0, format, type, pixels);
  }
  set_default_sampling();
  gl::BindTexture(gl::TEXTURE_2D, 0);
  t.w = w;
  t.h = h;
}

} // namespace

bool Renderer::create_program(std::string& err)
{
  const gl::GLuint vs = compile(gl::VERTEX_SHADER, kVertexShader, err);
  if(vs == 0)
  {
    return false;
  }
  const gl::GLuint fs = compile(gl::FRAGMENT_SHADER, kFragmentShader, err);
  if(fs == 0)
  {
    gl::DeleteShader(vs);
    return false;
  }
  prog_ = gl::CreateProgram();
  gl::AttachShader(prog_, vs);
  gl::AttachShader(prog_, fs);
  gl::LinkProgram(prog_);
  gl::DeleteShader(vs);
  gl::DeleteShader(fs);
  gl::GLint ok = 0;
  gl::GetProgramiv(prog_, gl::LINK_STATUS, &ok);
  if(ok == 0)
  {
    char log[kLogBytes] = {0};
    gl::GetProgramInfoLog(prog_, kLogBytes, nullptr, log);
    err = std::string("program link: ") + log;
    return false;
  }
  return true;
}

void Renderer::lookup_uniforms()
{
  loc_.rect = gl::GetUniformLocation(prog_, "u_rect");
  loc_.uv = gl::GetUniformLocation(prog_, "u_uv");
  loc_.viewport = gl::GetUniformLocation(prog_, "u_viewport");
  loc_.before = gl::GetUniformLocation(prog_, "u_before");
  loc_.after = gl::GetUniformLocation(prog_, "u_after");
  loc_.heat = gl::GetUniformLocation(prog_, "u_heat");
  loc_.cmap = gl::GetUniformLocation(prog_, "u_cmap");
  loc_.mode = gl::GetUniformLocation(prog_, "u_mode");
  loc_.overlay_base = gl::GetUniformLocation(prog_, "u_overlay_base");
  loc_.intensity = gl::GetUniformLocation(prog_, "u_intensity");
  loc_.heat_min = gl::GetUniformLocation(prog_, "u_heat_min");
  loc_.heat_max = gl::GetUniformLocation(prog_, "u_heat_max");
  loc_.gamma = gl::GetUniformLocation(prog_, "u_gamma");
  loc_.threshold = gl::GetUniformLocation(prog_, "u_threshold");
  loc_.proportional = gl::GetUniformLocation(prog_, "u_proportional");
  loc_.has_heat = gl::GetUniformLocation(prog_, "u_has_heat");
  loc_.split = gl::GetUniformLocation(prog_, "u_split");
  loc_.split_pos = gl::GetUniformLocation(prog_, "u_split_pos");
  loc_.split_invert = gl::GetUniformLocation(prog_, "u_split_invert");
  loc_.checker = gl::GetUniformLocation(prog_, "u_checker");
  CC_ENSURE(loc_.rect >= 0 && loc_.uv >= 0 && loc_.viewport >= 0 && loc_.mode >= 0);
}

void Renderer::create_quad()
{
  const float quad[kQuadVertices * 2] = {0, 0, 1, 0, 0, 1, 1, 1};
  gl::GenVertexArrays(1, &vao_);
  gl::GenBuffers(1, &vbo_);
  gl::BindVertexArray(vao_);
  gl::BindBuffer(gl::ARRAY_BUFFER, vbo_);
  gl::BufferData(gl::ARRAY_BUFFER, sizeof quad, quad, gl::STATIC_DRAW);
  gl::EnableVertexAttribArray(0);
  gl::VertexAttribPointer(0, 2, gl::FLOAT, gl::FALSE_, 0, nullptr);
  gl::BindVertexArray(0);
}

void Renderer::create_fallback_textures()
{
  gl::GenTextures(1, &cmap_);
  gl::BindTexture(gl::TEXTURE_2D, cmap_);
  set_default_sampling();
  set_colormap(cmap_kind_);

  /* 1x1 fallbacks so every sampler is always bound to something */
  const uint8_t grey[kImageChannels] = {kBlankGrey, kBlankGrey, kBlankGrey, kOpaqueAlpha};
  gl::GenTextures(1, &blank_);
  gl::BindTexture(gl::TEXTURE_2D, blank_);
  gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, 1, 1, 0, gl::RGBA, gl::UNSIGNED_BYTE, grey);
  set_filter(blank_, true);
  const float zero = 0.0f;
  gl::GenTextures(1, &zero_heat_);
  gl::BindTexture(gl::TEXTURE_2D, zero_heat_);
  gl::TexImage2D(gl::TEXTURE_2D, 0, gl::R32F, 1, 1, 0, gl::RED, gl::FLOAT, &zero);
  set_filter(zero_heat_, true);
  gl::BindTexture(gl::TEXTURE_2D, 0);
}

bool Renderer::init(std::string& err)
{
  CC_REQUIRE(gl::GetString != nullptr, err = "OpenGL entry points are not loaded"; return false);
  instance_ = this;
  drain_errors(); /* errors left over from context creation are not ours */
  const char* ver = reinterpret_cast<const char*>(gl::GetString(gl::VERSION));
  const char* ren = reinterpret_cast<const char*>(gl::GetString(gl::RENDERER));
  gl_info_ = std::string(ren != nullptr ? ren : "?") + " / OpenGL " + (ver != nullptr ? ver : "?");
  gl::GLint mt = 0;
  gl::GetIntegerv(gl::MAX_TEXTURE_SIZE, &mt);
  max_tex_ = mt > 0 ? mt : kDefaultMaxTexture;
  if(!create_program(err))
  {
    return false;
  }
  lookup_uniforms();
  create_quad();
  create_fallback_textures();
  if(!drain_errors())
  {
    err = "OpenGL reported an error while creating the compare shader/textures";
    return false;
  }
  return true;
}

void Renderer::shutdown()
{
  if(prog_ != 0)
  {
    gl::DeleteProgram(prog_);
  }
  if(vao_ != 0)
  {
    gl::DeleteVertexArrays(1, &vao_);
  }
  if(vbo_ != 0)
  {
    gl::DeleteBuffers(1, &vbo_);
  }
  if(cmap_ != 0)
  {
    gl::DeleteTextures(1, &cmap_);
  }
  if(blank_ != 0)
  {
    gl::DeleteTextures(1, &blank_);
  }
  if(zero_heat_ != 0)
  {
    gl::DeleteTextures(1, &zero_heat_);
  }
  prog_ = 0;
  vao_ = 0;
  vbo_ = 0;
  cmap_ = 0;
  blank_ = 0;
  zero_heat_ = 0;
  instance_ = nullptr;
}

void Renderer::set_colormap(ColorMap c)
{
  CC_REQUIRE(cmap_ != 0, return);
  cmap_kind_ = c;
  const std::vector<uint8_t> table = colormap_table(c);
  gl::BindTexture(gl::TEXTURE_2D, cmap_);
  gl::PixelStorei(gl::UNPACK_ALIGNMENT, kTightAlignment);
  gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGB8, kColorMapTableSize, 1, 0, gl::RGB, gl::UNSIGNED_BYTE, table.data());
  gl::PixelStorei(gl::UNPACK_ALIGNMENT, kDefaultAlignment);
  gl::BindTexture(gl::TEXTURE_2D, 0);
}

void Renderer::upload_rgba(GpuTexture& t, const Image& img)
{
  if(!img.valid())
  {
    release(t);
    return;
  }
  upload_2d(t, img.w, img.h, gl::RGBA8, gl::RGBA, gl::UNSIGNED_BYTE, img.rgba.data());
}

void Renderer::upload_heat(GpuTexture& t, const HeatMap& hm)
{
  if(!hm.valid())
  {
    release(t);
    return;
  }
  upload_2d(t, hm.w, hm.h, gl::R32F, gl::RED, gl::FLOAT, hm.v.data());
}

void Renderer::release(GpuTexture& t)
{
  if(t.id != 0)
  {
    gl::DeleteTextures(1, &t.id);
  }
  t = GpuTexture{};
}

void Renderer::draw_callback(const ImDrawList* /*list*/, const ImDrawCmd* cmd)
{
  if(instance_ != nullptr && cmd != nullptr && cmd->UserCallbackData != nullptr)
  {
    instance_->draw_frame(*static_cast<const FrameDraws*>(cmd->UserCallbackData));
  }
}

void Renderer::draw_frame(const FrameDraws& f)
{
  if(f.items.empty() || f.display_w <= 0 || f.display_h <= 0)
  {
    return;
  }
  CC_REQUIRE(prog_ != 0 && vao_ != 0, return);
  gl::UseProgram(prog_);
  gl::BindVertexArray(vao_);
  gl::Disable(gl::BLEND);
  gl::Enable(gl::SCISSOR_TEST);
  gl::Uniform2f(loc_.viewport, f.display_w, f.display_h);
  gl::Uniform1i(loc_.before, kUnitBefore);
  gl::Uniform1i(loc_.after, kUnitAfter);
  gl::Uniform1i(loc_.heat, kUnitHeat);
  gl::Uniform1i(loc_.cmap, kUnitCmap);
  for(const DrawParams& p : f.items)
  {
    draw_item(p, f);
  }
  gl::BindVertexArray(0);
  gl::UseProgram(0);
}

void Renderer::bind_item_textures(const DrawParams& p)
{
  const gl::GLuint before = (p.before != nullptr && p.before->valid()) ? p.before->id : blank_;
  const gl::GLuint after = (p.after != nullptr && p.after->valid()) ? p.after->id : blank_;
  const gl::GLuint heat = (p.heat != nullptr && p.heat->valid()) ? p.heat->id : zero_heat_;
  gl::ActiveTexture(gl::TEXTURE0 + static_cast<gl::GLenum>(kUnitBefore));
  set_filter(before, p.nearest);
  gl::ActiveTexture(gl::TEXTURE0 + static_cast<gl::GLenum>(kUnitAfter));
  set_filter(after, p.nearest);
  gl::ActiveTexture(gl::TEXTURE0 + static_cast<gl::GLenum>(kUnitHeat));
  set_filter(heat, p.nearest);
  gl::ActiveTexture(gl::TEXTURE0 + static_cast<gl::GLenum>(kUnitCmap));
  gl::BindTexture(gl::TEXTURE_2D, cmap_);
  gl::ActiveTexture(gl::TEXTURE0);
}

void Renderer::draw_item(const DrawParams& p, const FrameDraws& f)
{
  /* scissor: ImGui coordinates -> framebuffer pixels, origin bottom-left */
  const float cx = p.clip[0] * f.fb_scale_x;
  const float cy = p.clip[1] * f.fb_scale_y;
  const float cw = p.clip[2] * f.fb_scale_x;
  const float ch = p.clip[3] * f.fb_scale_y;
  if(cw <= 0 || ch <= 0)
  {
    return;
  }
  CC_REQUIRE(p.gamma > 0.0f, return);
  gl::Scissor(static_cast<gl::GLint>(std::floor(cx)), static_cast<gl::GLint>(std::floor(static_cast<float>(f.fb_h) - (cy + ch))),
              static_cast<gl::GLsizei>(std::ceil(cw)), static_cast<gl::GLsizei>(std::ceil(ch)));
  bind_item_textures(p);
  gl::Uniform4f(loc_.rect, p.rect[0], p.rect[1], p.rect[2], p.rect[3]);
  gl::Uniform4f(loc_.uv, p.uv[0], p.uv[1], p.uv[2], p.uv[3]);
  gl::Uniform1i(loc_.mode, static_cast<int>(p.mode));
  gl::Uniform1i(loc_.overlay_base, p.overlay_base);
  gl::Uniform1f(loc_.intensity, p.intensity);
  gl::Uniform1f(loc_.heat_min, p.heat_min);
  gl::Uniform1f(loc_.heat_max, p.heat_max);
  gl::Uniform1f(loc_.gamma, p.gamma);
  gl::Uniform1f(loc_.threshold, p.threshold);
  gl::Uniform1i(loc_.proportional, p.proportional ? 1 : 0);
  gl::Uniform1i(loc_.has_heat, (p.heat != nullptr && p.heat->valid()) ? 1 : 0);
  gl::Uniform1i(loc_.split, static_cast<int>(p.split));
  gl::Uniform2f(loc_.split_pos, p.split_pos[0], p.split_pos[1]);
  gl::Uniform1i(loc_.split_invert, p.split_invert ? 1 : 0);
  gl::Uniform1i(loc_.checker, p.checker ? 1 : 0);
  gl::DrawArrays(gl::TRIANGLE_STRIP, 0, kQuadVertices);
}

} // namespace cc
