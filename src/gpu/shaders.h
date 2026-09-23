/**
 * @file shaders.h
 * @brief GLSL 3.30 sources for the compare view.
 *
 * All the per-pixel work that CLIJ2 would do in OpenCL kernels has already
 * been done once on the CPU (the heat texture holds raw metric values); the
 * shader only scales, colour-maps, blends and splits, so redraws are
 * essentially free on an integrated GPU.
 */
#pragma once

namespace cc {

/** @brief Vertex shader: places a unit quad at a pixel rectangle and maps a UV sub-rectangle onto it. */
inline const char* const kVertexShader = R"GLSL(
#version 330 core
layout(location = 0) in vec2 a_pos;      // unit quad 0..1
uniform vec4 u_rect;                     // x, y, w, h in window pixels (y down)
uniform vec4 u_uv;                       // uv sub-rectangle x, y, w, h (0..1)
uniform vec2 u_viewport;                 // window size in pixels
out vec2 v_uv;
out vec2 v_px;
void main()
{
  v_uv = u_uv.xy + a_pos * u_uv.zw;
  v_px = u_rect.xy + a_pos * u_rect.zw;
  vec2 clip = vec2(v_px.x / u_viewport.x * 2.0 - 1.0, 1.0 - v_px.y / u_viewport.y * 2.0);
  gl_Position = vec4(clip, 0.0, 1.0);
}
)GLSL";

/** @brief Fragment shader: before / after / heat / overlay with an optional split divider. */
inline const char* const kFragmentShader = R"GLSL(
#version 330 core
in vec2 v_uv;
in vec2 v_px;
out vec4 frag;
uniform sampler2D u_before;   // original, resampled to the comparison size
uniform sampler2D u_after;    // the site's version
uniform sampler2D u_heat;     // raw metric values (R32F)
uniform sampler2D u_cmap;     // 256 x 1 colour map
uniform int   u_mode;         // 0 before, 1 after, 2 heat map, 3 heat map overlay
uniform int   u_overlay_base; // 0 before, 1 after
uniform float u_intensity;    // overlay opacity 0..1
uniform float u_heat_min;
uniform float u_heat_max;
uniform float u_gamma;        // curve applied to the normalised value
uniform float u_threshold;    // normalised cut-off below which nothing is drawn
uniform int   u_proportional; // 1: opacity also scales with the value
uniform int   u_has_heat;
uniform int   u_split;        // 0 none, 1 vertical divider, 2 horizontal divider
uniform vec2  u_split_pos;    // divider position in window pixels
uniform int   u_split_invert; // swap the two sides
uniform int   u_checker;      // draw a checkerboard under transparent pixels

const float kCheckerCellPx = 8.0;
const vec3  kCheckerDark = vec3(0.30);
const vec3  kCheckerLight = vec3(0.42);
const float kRangeEpsilon = 1e-6;

vec3 checker(vec2 px)
{
  float c = mod(floor(px.x / kCheckerCellPx) + floor(px.y / kCheckerCellPx), 2.0);
  return mix(kCheckerDark, kCheckerLight, c);
}

void main()
{
  vec4 before = texture(u_before, v_uv);
  vec4 after  = texture(u_after, v_uv);
  float raw = (u_has_heat == 1) ? texture(u_heat, v_uv).r : 0.0;
  float t = clamp((raw - u_heat_min) / max(u_heat_max - u_heat_min, kRangeEpsilon), 0.0, 1.0);
  t = pow(t, u_gamma);
  vec3 heat = texture(u_cmap, vec2(t, 0.5)).rgb;

  int mode = u_mode;
  if(u_split != 0)
  {
    bool first = (u_split == 1) ? (v_px.x < u_split_pos.x) : (v_px.y < u_split_pos.y);
    if(u_split_invert == 1) first = !first;
    mode = first ? 0 : u_mode;
  }

  vec3 col;
  float alpha = 1.0;
  if(mode == 0)      { col = before.rgb; alpha = before.a; }
  else if(mode == 1) { col = after.rgb;  alpha = after.a; }
  else if(mode == 2) { col = heat; }
  else
  {
    vec4 base = (u_overlay_base == 0) ? before : after;
    float a = u_intensity * ((t >= u_threshold) ? 1.0 : 0.0) * ((u_proportional == 1) ? t : 1.0);
    col = mix(base.rgb, heat, a);
    alpha = base.a;
  }
  if(u_checker == 1 && alpha < 1.0) col = mix(checker(v_px), col, alpha);
  frag = vec4(col, 1.0);
}
)GLSL";

} // namespace cc
