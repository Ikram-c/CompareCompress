/**
 * @file renderer.h
 * @brief OpenGL 3.3 renderer for the compare view.
 *
 * Owns the compare shader, the colour-map texture and helpers to upload
 * images / heat maps as textures.  Drawing happens inside a Dear ImGui
 * draw-list callback so it is ordered and clipped like any other UI element
 * (panels, tooltips and the loupe are drawn on top by ImGui).
 */
#pragma once

#include "core/colormap.h"
#include "core/image.h"
#include "core/metrics.h"
#include "core/view_mode.h"
#include "gpu/gl.h"

#include <string>
#include <vector>

struct ImDrawList;
struct ImDrawCmd;

namespace cc {

/** @brief A texture on the GPU with its size. */
struct GpuTexture
{
  gl::GLuint id = 0;
  int w = 0;
  int h = 0;
  [[nodiscard]] bool valid() const { return id != 0 && w > 0 && h > 0; }
};

/** @brief Kind of split divider. */
enum class SplitKind : int
{
  None = 0,
  Vertical = 1,
  Horizontal = 2,
};

/** @brief One quad to draw: where, which textures, how to blend. */
struct DrawParams
{
  float rect[4] = {0, 0, 0, 0}; /**< x, y, w, h in ImGui (window) coordinates */
  float uv[4] = {0, 0, 1, 1};   /**< texture sub-rectangle */
  float clip[4] = {0, 0, 0, 0}; /**< scissor rectangle in ImGui coordinates */
  ViewMode mode = ViewMode::After;
  int overlay_base = 1; /**< 0 before, 1 after */
  float intensity = 1.0f;
  float heat_min = 0.0f;
  float heat_max = 1.0f;
  float gamma = 1.0f; /**< already clamped to `heatmap.gamma_floor` by the caller */
  float threshold = 0.0f;
  bool proportional = false;
  SplitKind split = SplitKind::None;
  float split_pos[2] = {0, 0}; /**< divider position in window pixels */
  bool split_invert = false;
  bool nearest = false; /**< NEAREST filtering (>= 100 % zoom shows real pixels) */
  bool checker = true;  /**< checkerboard under transparent pixels */
  const GpuTexture* before = nullptr;
  const GpuTexture* after = nullptr;
  const GpuTexture* heat = nullptr;
};

/** @brief Everything the draw callback needs for one frame. */
struct FrameDraws
{
  std::vector<DrawParams> items;
  float display_w = 0; /**< ImGui display size */
  float display_h = 0;
  float fb_scale_x = 1;
  float fb_scale_y = 1;
  int fb_h = 0; /**< framebuffer height in pixels (for the scissor y flip) */
};

/** @brief The compare-view renderer; exactly one instance exists while the window is open. */
class Renderer
{
public:
  Renderer() = default;
  ~Renderer() = default;
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  /**
   * @brief Compiles the shader and creates the shared textures.
   * @param err Receives the compiler / linker log on failure.
   * @return true on success; gl::load() must have succeeded before.
   */
  [[nodiscard]] bool init(std::string& err);

  /** @brief Releases every GL object; safe to call twice. */
  void shutdown();

  /** @brief Uploads (or re-uploads) an RGBA image; releases the texture when the image is invalid. */
  void upload_rgba(GpuTexture& t, const Image& img);

  /** @brief Uploads a heat map as an R32F texture; releases the texture when the map is invalid. */
  void upload_heat(GpuTexture& t, const HeatMap& hm);

  /** @brief Deletes a texture. */
  void release(GpuTexture& t);

  /** @brief Replaces the colour-map texture. */
  void set_colormap(ColorMap c);

  /** @brief The colour map currently uploaded. */
  [[nodiscard]] ColorMap colormap() const { return cmap_kind_; }

  /**
   * @brief ImGui draw-list callback.
   * @param list Unused.
   * @param cmd `cmd->UserCallbackData` must point at a FrameDraws that outlives the frame.
   */
  static void draw_callback(const ImDrawList* list, const ImDrawCmd* cmd);

  /** @brief The live renderer, or null before init() / after shutdown(). */
  [[nodiscard]] static Renderer* instance() { return instance_; }

  /** @brief GL_MAX_TEXTURE_SIZE of the context. */
  [[nodiscard]] int max_texture_size() const { return max_tex_; }

  /** @brief "renderer / OpenGL version" string for the About window. */
  [[nodiscard]] const std::string& gl_info() const { return gl_info_; }

private:
  /** @brief Uniform locations of the compare program. */
  struct Locations
  {
    gl::GLint rect = -1;
    gl::GLint uv = -1;
    gl::GLint viewport = -1;
    gl::GLint before = -1;
    gl::GLint after = -1;
    gl::GLint heat = -1;
    gl::GLint cmap = -1;
    gl::GLint mode = -1;
    gl::GLint overlay_base = -1;
    gl::GLint intensity = -1;
    gl::GLint heat_min = -1;
    gl::GLint heat_max = -1;
    gl::GLint gamma = -1;
    gl::GLint threshold = -1;
    gl::GLint proportional = -1;
    gl::GLint has_heat = -1;
    gl::GLint split = -1;
    gl::GLint split_pos = -1;
    gl::GLint split_invert = -1;
    gl::GLint checker = -1;
  };

  bool create_program(std::string& err);
  void lookup_uniforms();
  void create_quad();
  void create_fallback_textures();
  void draw_frame(const FrameDraws& f);
  void draw_item(const DrawParams& p, const FrameDraws& f);
  void bind_item_textures(const DrawParams& p);

  static Renderer* instance_;
  gl::GLuint prog_ = 0;
  gl::GLuint vao_ = 0;
  gl::GLuint vbo_ = 0;
  gl::GLuint cmap_ = 0;
  gl::GLuint blank_ = 0;
  gl::GLuint zero_heat_ = 0;
  int max_tex_ = 0;
  std::string gl_info_;
  ColorMap cmap_kind_ = ColorMap::Inferno;
  Locations loc_;
};

} // namespace cc
