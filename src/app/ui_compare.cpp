/**
 * @file ui_compare.cpp
 * @brief The compare view: Lightroom-style layouts (Loupe, Before/After
 * Left/Right and Top/Bottom, their Split variants, Survey), synchronised
 * zoom/pan, a draggable split divider, hover read-outs and a corner loupe.
 *
 * Behaviour references (see docs/explanation/references.md): split position
 * kept as a 0..1 fraction with click-drag and a rotate handle (darktable
 * snapshots), a few-pixel hit radius on the divider, zoom steps and snapping
 * (darktable develop), NEAREST filtering at >= 1:1 and linked zoom/pan across
 * panes (darktable culling / Lightroom "Link Focus").
 */
#include "app/app.h"

#include "app/ui_style.h"
#include "core/sites.h"
#include "platform/dialogs.h"
#include "util/contract.h"
#include "util/strings.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>
#include <vector>

namespace cc {

namespace {

/** @brief A rectangle of the canvas that shows one image. */
struct Pane
{
  ImVec2 p0;
  ImVec2 p1;
  [[nodiscard]] float w() const { return p1.x - p0.x; }
  [[nodiscard]] float h() const { return p1.y - p0.y; }
  [[nodiscard]] ImVec2 center() const { return ImVec2((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f); }
};

/** @brief Screen rect of the image inside a pane for the current zoom / centre. */
struct Placement
{
  float zoom = 1.0f; /**< screen px per image px */
  ImVec2 origin;     /**< screen position of image pixel (0, 0) */
};

/**
 * @brief Places an image in a pane.
 * @param pane Screen rectangle of the pane.
 * @param iw Image width in pixels.
 * @param ih Image height in pixels.
 * @param v Zoom / pan state; the centre is clamped to the real extent.
 * @param fit_out Receives the "fit" zoom.
 * @return Zoom and origin that place the image.
 */
Placement place_image(const Pane& pane, int iw, int ih, ViewState& v, float& fit_out)
{
  Placement p;
  const float fit = std::max(1e-4f, std::min(pane.w() / static_cast<float>(std::max(1, iw)), pane.h() / static_cast<float>(std::max(1, ih))));
  fit_out = fit;
  p.zoom = v.zoom > 0.0f ? v.zoom : fit;
  const float sw = static_cast<float>(iw) * p.zoom;
  const float sh = static_cast<float>(ih) * p.zoom;
  /* darktable culling: pan bounded to the real extent; small images stay centred */
  v.center_x = sw <= pane.w() ? 0.5f : std::clamp(v.center_x, pane.w() / (2.0f * sw), 1.0f - pane.w() / (2.0f * sw));
  v.center_y = sh <= pane.h() ? 0.5f : std::clamp(v.center_y, pane.h() / (2.0f * sh), 1.0f - pane.h() / (2.0f * sh));
  const ImVec2 c = pane.center();
  p.origin = ImVec2(c.x - v.center_x * sw, c.y - v.center_y * sh);
  return p;
}

const char* mode_label(ViewMode m)
{
  switch(m)
  {
    case ViewMode::Before: return "Before (original)";
    case ViewMode::After: return "After";
    case ViewMode::Heat: return "Heat map";
    case ViewMode::Overlay: return "After + heat map";
  }
  return "";
}

/** @brief Text on a translucent box. */
void label_box(ImDrawList* dl, ImVec2 at, const std::string& text, ImU32 bg = style::kColLabelBg)
{
  const ImVec2 sz = ImGui::CalcTextSize(text.c_str());
  dl->AddRectFilled(ImVec2(at.x - style::kLabelPadXPx, at.y - style::kLabelPadYPx),
                    ImVec2(at.x + sz.x + style::kLabelPadXPx, at.y + sz.y + style::kLabelPadYPx), bg, style::kInnerRadiusPx);
  dl->AddText(at, style::kColLabelText, text.c_str());
}

/** @brief Fills the rect / clip / filtering of a draw item from a placement and a pane. */
void set_geometry(DrawParams& d, const Placement& pl, const Pane& pane, int iw, int ih)
{
  d.rect[0] = pl.origin.x;
  d.rect[1] = pl.origin.y;
  d.rect[2] = static_cast<float>(iw) * pl.zoom;
  d.rect[3] = static_cast<float>(ih) * pl.zoom;
  d.clip[0] = pane.p0.x;
  d.clip[1] = pane.p0.y;
  d.clip[2] = pane.w();
  d.clip[3] = pane.h();
  d.nearest = pl.zoom >= 1.0f;
}

} // namespace

/* ---- keyboard --------------------------------------------------------------- */

namespace {
bool pressed(ImGuiKey k) { return ImGui::IsKeyPressed(k, false); }
} // namespace

void App::handle_view_keys(bool shift, bool alt)
{
  if(pressed(ImGuiKey_Y))
  {
    if(alt)
    {
      set_layout(layout_ == Layout::TopBottom ? Layout::Loupe : Layout::TopBottom);
    }
    else if(shift)
    {
      /* Lightroom: Shift+Y toggles the split variant of the current orientation */
      const bool top_bottom = layout_ == Layout::TopBottom || layout_ == Layout::TopBottomSplit;
      if(top_bottom)
      {
        set_layout(layout_ == Layout::TopBottomSplit ? Layout::TopBottom : Layout::TopBottomSplit);
      }
      else
      {
        set_layout(layout_ == Layout::LeftRightSplit ? Layout::LeftRight : Layout::LeftRightSplit);
      }
    }
    else
    {
      set_layout(layout_ == Layout::LeftRight ? Layout::Loupe : Layout::LeftRight);
    }
  }
  if(pressed(ImGuiKey_Backslash))
  {
    if(layout_ == Layout::Loupe)
    {
      view_.loupe_show_before = !view_.loupe_show_before;
    }
    else
    {
      view_.split_invert = !view_.split_invert;
    }
  }
  if(pressed(ImGuiKey_E))
  {
    set_layout(Layout::Loupe);
  }
  if(pressed(ImGuiKey_N) && sites_.size() > 1)
  {
    set_layout(Layout::Survey);
  }
  if(pressed(ImGuiKey_RightArrow) && !sites_.empty())
  {
    active_site_ = (active_site_ + 1) % static_cast<int>(sites_.size());
  }
  if(pressed(ImGuiKey_LeftArrow) && !sites_.empty())
  {
    active_site_ = (active_site_ + static_cast<int>(sites_.size()) - 1) % static_cast<int>(sites_.size());
  }
  if(pressed(ImGuiKey_Space) && layout_ == Layout::Loupe)
  {
    view_.loupe_show_before = true;
  }
  if(ImGui::IsKeyReleased(ImGuiKey_Space) && layout_ == Layout::Loupe)
  {
    view_.loupe_show_before = false;
  }
}

void App::handle_zoom_keys(bool ctrl)
{
  if(pressed(ImGuiKey_Z))
  {
    toggle_zoom_1_1();
  }
  if(pressed(ImGuiKey_F))
  {
    view_.reset_zoom();
  }
  if(ctrl && (pressed(ImGuiKey_Equal) || pressed(ImGuiKey_KeypadAdd)))
  {
    zoom_step(+1, view_.center_x, view_.center_y);
  }
  if(ctrl && (pressed(ImGuiKey_Minus) || pressed(ImGuiKey_KeypadSubtract)))
  {
    zoom_step(-1, view_.center_x, view_.center_y);
  }
  if(pressed(ImGuiKey_Tab) && !ctrl)
  {
    const bool any = show_left_panel_ || show_right_panel_;
    show_left_panel_ = !any;
    show_right_panel_ = !any;
  }
  if(pressed(ImGuiKey_L))
  {
    settings_.show_loupe = !settings_.show_loupe;
  }
}

void App::handle_heat_keys(bool shift)
{
  if(pressed(ImGuiKey_H))
  {
    if(shift)
    {
      heat_.after_mode = heat_.after_mode == ViewMode::Heat ? ViewMode::After : ViewMode::Heat;
    }
    else
    {
      heat_.after_mode = heat_.after_mode == ViewMode::Overlay ? ViewMode::After : ViewMode::Overlay;
    }
  }
  const float step = config_.heatmap.intensity_step;
  if(pressed(ImGuiKey_LeftBracket))
  {
    heat_.intensity = std::clamp(heat_.intensity - step, 0.0f, 1.0f);
  }
  if(pressed(ImGuiKey_RightBracket))
  {
    heat_.intensity = std::clamp(heat_.intensity + step, 0.0f, 1.0f);
  }
  if(pressed(ImGuiKey_B))
  {
    heat_.block_view = !heat_.block_view;
  }
  if(pressed(ImGuiKey_M))
  {
    const int delta = shift ? kMetricCount - 1 : 1;
    heat_.metric = static_cast<Metric>((static_cast<int>(heat_.metric) + delta) % kMetricCount);
  }
}

void App::handle_file_keys(bool ctrl)
{
  if(pressed(ImGuiKey_F1))
  {
    show_about_ = !show_about_;
  }
  if(ctrl && pressed(ImGuiKey_O) && dialogs_.available)
  {
    Slot* dest = next_empty_slot(nullptr);
    std::string path;
    if(dest != nullptr && open_file_dialog(dialogs_, path, "Choose the " + dest->title + " file", true))
    {
      load_slot_path(*dest, path);
    }
  }
  if(ctrl && (pressed(ImGuiKey_Comma) || pressed(ImGuiKey_P)))
  {
    show_settings_ = !show_settings_;
  }
  if(ctrl && pressed(ImGuiKey_V))
  {
    load_from_clipboard(hovered_drop_slot_ >= 0 ? slot_by_id(hovered_drop_slot_) : nullptr);
  }
  if(pressed(ImGuiKey_C) && !ctrl)
  {
    set_layout(Layout::LeftRight);
  }
}

void App::handle_shortcuts()
{
  const ImGuiIO& io = ImGui::GetIO();
  if(io.WantTextInput)
  {
    return;
  }
  handle_view_keys(io.KeyShift, io.KeyAlt);
  handle_zoom_keys(io.KeyCtrl);
  handle_heat_keys(io.KeyShift);
  handle_file_keys(io.KeyCtrl);
}

/* ---- toolbar ---------------------------------------------------------------- */

void App::ui_viewer_site_selector()
{
  const int n = static_cast<int>(sites_.size());
  if(ImGui::ArrowButton("prev", ImGuiDir_Left))
  {
    active_site_ = (active_site_ + n - 1) % n;
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(std::min(style::kSiteComboWidthPx * ui_scale(), ImGui::GetContentRegionAvail().x * style::kSiteComboMaxFraction));
  Slot* site = active_site();
  if(ImGui::BeginCombo("##site", site != nullptr ? site->site_label(config_.sites).c_str() : "-"))
  {
    for(int i = 0; i < n; ++i)
    {
      ImGui::PushID(i);
      if(ImGui::Selectable(sites_[static_cast<std::size_t>(i)].site_label(config_.sites).c_str(), i == active_site_))
      {
        active_site_ = i;
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  HelpTooltip("Site shown on the 'After' side (Left/Right arrow keys)");
  ImGui::SameLine();
  if(ImGui::ArrowButton("next", ImGuiDir_Right))
  {
    active_site_ = (active_site_ + 1) % n;
  }
  ImGui::SameLine();
}

void App::ui_viewer_toolbar()
{
  if(ImGui::Button("Change view"))
  {
    cycle_layout(+1);
  }
  HelpTooltip("Cycle Lightroom-style views: Loupe > Left/Right > L/R Split > Top/Bottom > T/B Split > Survey\n"
              "Keys: Y  Alt+Y  Shift+Y  \\  E  C  N");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(std::min(style::kToolbarComboWidthPx * ui_scale(), ImGui::GetContentRegionAvail().x * style::kToolbarComboMaxFraction));
  if(ImGui::BeginCombo("##layout", layout_name(layout_)))
  {
    for(int i = 0; i < kLayoutCount; ++i)
    {
      const Layout l = static_cast<Layout>(i);
      ImGui::BeginDisabled(l == Layout::Survey && sites_.size() < 2);
      if(ImGui::Selectable(layout_name(l), layout_ == l))
      {
        set_layout(l);
      }
      ImGui::EndDisabled();
    }
    ImGui::EndCombo();
  }
  HelpTooltip("Loupe: one image, \\ flips before/after.  Left/Right & Top/Bottom: both side by side.\n"
              "Split: one image with a draggable divider.  Survey: every site at once.");
  if(sites_.size() > 1)
  {
    ui_viewer_site_selector();
  }
  ui_viewer_zoom_controls();
}

void App::ui_viewer_zoom_controls()
{
  /* zoom read-out (darktable thumbnail: "fit" or percentage) */
  char zoomtxt[32];
  if(view_.zoom == 0.0f)
  {
    std::snprintf(zoomtxt, sizeof zoomtxt, "fit");
  }
  else
  {
    std::snprintf(zoomtxt, sizeof zoomtxt, "%.0f%%", static_cast<double>(view_.zoom) * 100.0);
  }
  if(ImGui::Button(zoomtxt))
  {
    toggle_zoom_1_1();
  }
  HelpTooltip("Zoom: wheel over the image (about the cursor), Z toggles fit/100%/200%, F fits.\nDrag to pan. "
              "Zoom and pan are linked across panes (Link Focus).");
  ImGui::SameLine();
  if(ImGui::SmallButton(view_.link_focus ? "linked" : "unlinked"))
  {
    view_.link_focus = !view_.link_focus;
  }
  HelpTooltip("Lightroom 'Link Focus': keep both sides at the same zoom and position.");
  if(layout_ != Layout::Loupe && layout_ != Layout::Survey)
  {
    ImGui::SameLine();
    if(ImGui::SmallButton("swap"))
    {
      view_.split_invert = !view_.split_invert;
    }
    HelpTooltip("Swap Before and After sides (\\)");
  }
  if(layout_ == Layout::Loupe)
  {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", view_.loupe_show_before ? "showing BEFORE  (\\ or hold Space)" : "showing AFTER  (\\ = before)");
  }
}

/* ---- the canvas ------------------------------------------------------------- */

struct LoupeGeometry;

/**
 * @brief Per-frame state of the compare canvas, split into steps so that each
 * stays short.  A friend of App so the steps can reach its state directly.
 */
struct ViewerFrame
{
  App& app;
  ImDrawList* dl = nullptr;
  Pane canvas;
  ImVec2 avail;
  bool canvas_hovered = false;
  bool canvas_active = false;
  Slot* site = nullptr;
  Comparison* comp = nullptr;
  std::vector<std::pair<Pane, ViewMode>> panes; /**< pane + what it shows (Before or the after mode) */
  std::vector<Placement> placements;
  float fit = 1.0f;
  float heat_max = 1.0f;
  bool near_divider = false; /**< pointer on the split line/handle: suppress the pixel read-out */

  explicit ViewerFrame(App& a) : app(a) {}

  [[nodiscard]] bool is_split() const { return app.layout_ == Layout::LeftRightSplit || app.layout_ == Layout::TopBottomSplit; }
  [[nodiscard]] bool vertical_split() const { return app.layout_ == Layout::LeftRightSplit; }
  [[nodiscard]] std::string after_label() const
  {
    return std::string(mode_label(app.heat_.after_mode)) + (site != nullptr ? " - " + site->site_label(app.config_.sites) : "");
  }

  void fill_params(DrawParams& d, const Comparison& c, ViewMode mode) const;
  bool begin_canvas();
  void draw_empty_state();
  void build_panes();
  void draw_survey();
  void draw_survey_tile(const Pane& tile, Comparison& c, bool is_original);
  void draw_panes();
  void draw_split_labels(const Pane& pane, float sx, float sy);
  void draw_split_divider();
  void split_interaction(const Pane& pane, bool near_line, bool near_handle);
  void handle_mouse();
  void hover_readout();
  void draw_loupe();
  void draw_loupe_box(const LoupeGeometry& g, ImVec2 b0, ViewMode mode, const char* name);
};

void ViewerFrame::fill_params(DrawParams& d, const Comparison& c, ViewMode mode) const
{
  d.mode = mode;
  d.overlay_base = app.heat_.overlay_base;
  d.intensity = app.heat_.intensity;
  d.heat_min = 0.0f;
  d.heat_max = heat_max;
  d.gamma = app.clamped_gamma();
  d.threshold = app.heat_.threshold;
  d.proportional = app.heat_.proportional;
  d.before = &c.tex_before;
  d.after = &c.tex_after;
  d.heat = c.heat.valid() ? &c.tex_heat : nullptr;
}

bool ViewerFrame::begin_canvas()
{
  const ImGuiIO& io = ImGui::GetIO();
  app.draws_.display_w = io.DisplaySize.x;
  app.draws_.display_h = io.DisplaySize.y;
  app.draws_.fb_scale_x = io.DisplayFramebufferScale.x;
  app.draws_.fb_scale_y = io.DisplayFramebufferScale.y;
  app.draws_.fb_h = static_cast<int>(std::lround(io.DisplaySize.y * io.DisplayFramebufferScale.y));
  dl = ImGui::GetWindowDrawList();
  const ImVec2 c0 = ImGui::GetCursorScreenPos();
  avail = ImGui::GetContentRegionAvail();
  const float min_px = app.config_.view.canvas_min_px;
  if(avail.x < min_px || avail.y < min_px)
  {
    return false;
  }
  canvas = Pane{c0, ImVec2(c0.x + avail.x, c0.y + avail.y)};
  ImGui::InvisibleButton("canvas", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
  canvas_hovered = ImGui::IsItemHovered();
  canvas_active = ImGui::IsItemActive();
  dl->AddRectFilled(canvas.p0, canvas.p1, style::kColCanvasBg);
  dl->AddCallback(Renderer::draw_callback, &app.draws_);
#if IMGUI_VERSION_NUM >= 19280
  dl->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr);
#else
  dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
#endif
  site = app.active_site();
  comp = app.active_comparison();
  heat_max = app.heat_scale_max(comp);
  return true;
}

void ViewerFrame::draw_empty_state()
{
  const ImVec2 c0 = canvas.p0;
  std::string msg;
  if(app.worker_.busy())
  {
    msg = app.worker_.current_label() + "...";
  }
  else if(!app.original_.media.loaded())
  {
    msg = "Load the original image or video in the left panel";
  }
  else if(site == nullptr || !site->media.loaded())
  {
    msg = "Load the version you downloaded from the site";
  }
  else if(comp != nullptr && !comp->error.empty())
  {
    msg = comp->error;
  }
  else
  {
    msg = "Press 'Visualize compression'";
  }
  if(site != nullptr && site->thumb.valid())
  {
    const float s = std::min(avail.x / static_cast<float>(site->thumb.w), avail.y / static_cast<float>(site->thumb.h)) * style::kPreviewShrink;
    const ImVec2 sz(static_cast<float>(site->thumb.w) * s, static_cast<float>(site->thumb.h) * s);
    const ImVec2 a(c0.x + (avail.x - sz.x) * 0.5f, c0.y + (avail.y - sz.y) * 0.5f);
    const int alpha = app.config_.view.preview_alpha;
    dl->AddImage(style::texture_id(site->thumb.id), a, ImVec2(a.x + sz.x, a.y + sz.y), ImVec2(0, 0), ImVec2(1, 1),
                 IM_COL32(255, 255, 255, alpha));
  }
  const float inset = style::kInsetLargePx;
  label_box(dl, ImVec2(c0.x + inset, c0.y + inset), msg);
  if(app.worker_.busy())
  {
    const float f = std::clamp(app.worker_.progress().fraction.load(), style::kMinProgressFraction, 1.0f);
    const float top = c0.y + style::kProgressBarTopPx;
    dl->AddRectFilled(ImVec2(c0.x + inset, top), ImVec2(c0.x + inset + app.config_.view.progress_bar_width_px * f, top + style::kProgressBarHeightLargePx),
                      style::kColProgress, style::kBarRadiusPx);
  }
}

void ViewerFrame::build_panes()
{
  const float gap = app.config_.view.pane_gap_px;
  const ImVec2 c0 = canvas.p0;
  const ViewMode after_mode = app.heat_.after_mode;
  ViewMode first = ViewMode::Before;
  ViewMode second = after_mode;
  if(app.view_.split_invert)
  {
    std::swap(first, second);
  }
  switch(app.layout_)
  {
    case Layout::Loupe: panes.push_back({canvas, app.view_.loupe_show_before ? ViewMode::Before : after_mode}); break;
    case Layout::LeftRight:
    {
      const float half = (avail.x - gap) * 0.5f;
      panes.push_back({Pane{c0, ImVec2(c0.x + half, canvas.p1.y)}, first});
      panes.push_back({Pane{ImVec2(c0.x + half + gap, c0.y), canvas.p1}, second});
      break;
    }
    case Layout::TopBottom:
    {
      const float half = (avail.y - gap) * 0.5f;
      panes.push_back({Pane{c0, ImVec2(canvas.p1.x, c0.y + half)}, first});
      panes.push_back({Pane{ImVec2(c0.x, c0.y + half + gap), canvas.p1}, second});
      break;
    }
    case Layout::LeftRightSplit:
    case Layout::TopBottomSplit: panes.push_back({canvas, after_mode}); break;
    case Layout::Survey:
    case Layout::COUNT: break;
  }
}

void ViewerFrame::draw_survey()
{
  const float gap = app.config_.view.pane_gap_px;
  const ImVec2 c0 = canvas.p0;
  std::vector<Comparison*> tiles;
  for(const Slot& s : app.sites_)
  {
    Comparison* c = app.comparison_for(s.id);
    if(c != nullptr && c->ok)
    {
      tiles.push_back(c);
    }
  }
  const int n = static_cast<int>(tiles.size()) + 1;
  const double aspect = static_cast<double>(avail.x) / static_cast<double>(std::max(1.0f, avail.y));
  const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n) * aspect))));
  const int rows = (n + cols - 1) / cols;
  const float tw = (avail.x - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
  const float th = (avail.y - gap * static_cast<float>(rows - 1)) / static_cast<float>(rows);
  for(int i = 0; i < n; ++i)
  {
    const int col = i % cols;
    const int row = i / cols;
    const float tx = c0.x + static_cast<float>(col) * (tw + gap);
    const float ty = c0.y + static_cast<float>(row) * (th + gap);
    const Pane tile{ImVec2(tx, ty), ImVec2(tx + tw, ty + th)};
    draw_survey_tile(tile, i == 0 ? *comp : *tiles[static_cast<std::size_t>(i - 1)], i == 0);
  }
}

void ViewerFrame::draw_survey_tile(const Pane& tile, Comparison& c, bool is_original)
{
  ViewState fitv;
  float tile_fit = 1.0f;
  const Placement pl = place_image(tile, c.w, c.h, fitv, tile_fit);
  DrawParams d;
  fill_params(d, c, is_original ? ViewMode::Before : app.heat_.after_mode);
  set_geometry(d, pl, tile, c.w, c.h);
  app.draws_.items.push_back(d);
  const Slot* owner = app.slot_by_id(c.slot_id);
  const std::string label = is_original ? "Original" : (owner != nullptr ? owner->site_label(app.config_.sites) : "?");
  dl->PushClipRect(tile.p0, tile.p1, true);
  label_box(dl, ImVec2(tile.p0.x + style::kInsetSmallPx, tile.p0.y + style::kInsetSmallPx), label);
  if(!is_original)
  {
    char buf[style::kLineBufChars];
    std::snprintf(buf, sizeof buf, "PSNR %.1f dB  SSIM %.3f  dE %.2f", c.stats.psnr, c.stats.ssim, c.stats.mean_de2000);
    label_box(dl, ImVec2(tile.p0.x + style::kInsetSmallPx, tile.p0.y + style::kInsetPx + ImGui::GetTextLineHeight()), buf, style::kColLabelBgLight);
  }
  dl->PopClipRect();
  const bool active_tile = !is_original && owner == site;
  dl->AddRect(tile.p0, tile.p1, active_tile ? style::kColTileActive : style::kColTileBorder);
  if(active_tile)
  {
    dl->AddRect(ImVec2(tile.p0.x + 1, tile.p0.y + 1), ImVec2(tile.p1.x - 1, tile.p1.y - 1), style::kColTileActive);
  }
  if(canvas_hovered && !is_original && ImGui::IsMouseHoveringRect(tile.p0, tile.p1))
  {
    if(ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
      for(int k = 0; k < static_cast<int>(app.sites_.size()); ++k)
      {
        if(app.sites_[static_cast<std::size_t>(k)].id == c.slot_id)
        {
          app.active_site_ = k;
        }
      }
    }
    if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
      app.set_layout(Layout::Loupe);
    }
    HelpTooltip("Click: select this site. Double-click: open in Loupe view.");
  }
}

void ViewerFrame::draw_panes()
{
  const int iw = comp->w;
  const int ih = comp->h;
  for(const std::pair<Pane, ViewMode>& entry : panes)
  {
    const Pane& pane = entry.first;
    const Placement pl = place_image(pane, iw, ih, app.view_, fit);
    placements.push_back(pl);
    DrawParams d;
    fill_params(d, *comp, entry.second);
    set_geometry(d, pl, pane, iw, ih);
    if(is_split())
    {
      d.split = vertical_split() ? SplitKind::Vertical : SplitKind::Horizontal;
      d.split_pos[0] = pane.p0.x + app.view_.split * pane.w();
      d.split_pos[1] = pane.p0.y + app.view_.split * pane.h();
      d.split_invert = app.view_.split_invert;
    }
    app.draws_.items.push_back(d);
  }
  app.fit_scale_ = fit;
  if(is_split())
  {
    draw_split_divider();
    return;
  }
  /* labels (Lightroom shows "Before" / "After" on each side) */
  for(const std::pair<Pane, ViewMode>& entry : panes)
  {
    const Pane& pane = entry.first;
    label_box(dl, ImVec2(pane.p0.x + style::kInsetPx, pane.p0.y + style::kInsetPx),
              entry.second == ViewMode::Before ? "Before (original)" : after_label());
    if(panes.size() > 1)
    {
      dl->AddRect(pane.p0, pane.p1, style::kColPaneBorder);
    }
  }
}

void ViewerFrame::draw_split_labels(const Pane& pane, float /*sx*/, float sy)
{
  const std::string before = "Before (original)";
  const std::string l1 = app.view_.split_invert ? after_label() : before;
  const std::string l2 = app.view_.split_invert ? before : after_label();
  const float inset = style::kInsetPx;
  label_box(dl, ImVec2(pane.p0.x + inset, pane.p0.y + inset), l1);
  if(vertical_split())
  {
    const ImVec2 sz = ImGui::CalcTextSize(l2.c_str());
    label_box(dl, ImVec2(pane.p1.x - sz.x - inset, pane.p0.y + inset), l2);
  }
  else
  {
    label_box(dl, ImVec2(pane.p0.x + inset, sy + inset), l2);
  }
}

void ViewerFrame::draw_split_divider()
{
  const SplitConfig& sc = app.config_.view.split;
  const Pane& pane = panes[0].first;
  const bool vertical = vertical_split();
  const float sx = pane.p0.x + app.view_.split * pane.w();
  const float sy = pane.p0.y + app.view_.split * pane.h();
  draw_split_labels(pane, sx, sy);
  /* divider: a thin line, brighter handle when the pointer is near (darktable snapshots) */
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const float scale = app.ui_scale();
  const float dist = vertical ? std::fabs(mouse.x - sx) : std::fabs(mouse.y - sy);
  const bool near_line = canvas_hovered && dist < sc.line_hit_px * scale;
  const ImVec2 hc = vertical ? ImVec2(sx, pane.center().y) : ImVec2(pane.center().x, sy);
  const float hd = std::hypot(mouse.x - hc.x, mouse.y - hc.y);
  const bool near_handle = canvas_hovered && hd < sc.handle_hit_px * scale;
  near_divider = near_line || near_handle;
  const ImU32 line_col = near_line ? style::kColDividerNear : style::kColDivider;
  const float line_w = near_line ? style::kDividerWidthNearPx : style::kDividerWidthPx;
  if(vertical)
  {
    dl->AddLine(ImVec2(sx, pane.p0.y), ImVec2(sx, pane.p1.y), line_col, line_w);
  }
  else
  {
    dl->AddLine(ImVec2(pane.p0.x, sy), ImVec2(pane.p1.x, sy), line_col, line_w);
  }
  const float hr = std::min(sc.handle_radius_max_px, sc.handle_radius_fraction * pane.w()) * scale * style::kHandleFillFactor + style::kHandleExtraPx;
  dl->AddCircleFilled(hc, hr, near_handle ? style::kColHandleFillNear : style::kColHandleFill);
  dl->AddCircle(hc, hr, near_handle ? style::kColHandleLineNear : style::kColHandleLine, 0, style::kHandleOutlinePx);
  /* rotate glyph: two arcs */
  const ImU32 glyph = near_handle ? style::kColHandleLineNear : style::kColHandleGlyph;
  dl->PathArcTo(hc, hr * style::kHandleArcFraction, style::kArcAStart, style::kArcAEnd, style::kHandleArcSegments);
  dl->PathStroke(glyph);
  dl->PathArcTo(hc, hr * style::kHandleArcFraction, style::kArcBStart, style::kArcBEnd, style::kHandleArcSegments);
  dl->PathStroke(glyph);
  split_interaction(pane, near_line, near_handle);
}

void ViewerFrame::split_interaction(const Pane& pane, bool near_line, bool near_handle)
{
  const bool vertical = vertical_split();
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  if(near_handle && !app.view_.dragging_split)
  {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    HelpTooltip("Click: rotate the split (vertical <-> horizontal)\nDrag the line to move it, double-click to centre");
    if(ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
      app.set_layout(vertical ? Layout::TopBottomSplit : Layout::LeftRightSplit);
      app.view_.split = std::clamp(vertical ? (mouse.y - pane.p0.y) / pane.h() : (mouse.x - pane.p0.x) / pane.w(), 0.0f, 1.0f);
    }
  }
  else if(near_line || app.view_.dragging_split)
  {
    ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
    if(near_line && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
      app.view_.dragging_split = true;
    }
    if(near_line && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
      app.view_.split = 0.5f;
    }
  }
  if(app.view_.dragging_split)
  {
    if(!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
      app.view_.dragging_split = false;
    }
    else
    {
      app.view_.split = std::clamp(vertical ? (mouse.x - pane.p0.x) / pane.w() : (mouse.y - pane.p0.y) / pane.h(), 0.0f, 1.0f);
    }
  }
}

void ViewerFrame::handle_mouse()
{
  const ImGuiIO& io = ImGui::GetIO();
  const int iw = comp->w;
  const int ih = comp->h;
  int hovered_pane = -1;
  for(std::size_t i = 0; i < panes.size(); ++i)
  {
    if(ImGui::IsMouseHoveringRect(panes[i].first.p0, panes[i].first.p1))
    {
      hovered_pane = static_cast<int>(i);
    }
  }
  if(canvas_hovered && hovered_pane >= 0 && !app.view_.dragging_split)
  {
    const Placement& pl = placements[static_cast<std::size_t>(hovered_pane)];
    const float fx = (io.MousePos.x - pl.origin.x) / pl.zoom;
    const float fy = (io.MousePos.y - pl.origin.y) / pl.zoom;
    const int px = static_cast<int>(std::floor(fx));
    const int py = static_cast<int>(std::floor(fy));
    if(px >= 0 && py >= 0 && px < iw && py < ih)
    {
      app.hover_valid_ = true;
      app.hover_px_ = px;
      app.hover_py_ = py;
    }
    if(io.MouseWheel != 0.0f)
    {
      app.zoom_step(io.MouseWheel > 0 ? +1 : -1, fx / static_cast<float>(iw), fy / static_cast<float>(ih));
    }
    if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !is_split())
    {
      app.toggle_zoom_1_1();
    }
    if(ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
    {
      app.toggle_zoom_1_1();
    }
  }
  if(canvas_active && !app.view_.dragging_split && ImGui::IsMouseDragging(ImGuiMouseButton_Left, app.config_.view.pan_drag_threshold_px))
  {
    app.view_.panning = true;
    const Placement& pl = placements[static_cast<std::size_t>(std::max(0, hovered_pane))];
    const float sw = static_cast<float>(iw) * pl.zoom;
    const float sh = static_cast<float>(ih) * pl.zoom;
    app.view_.center_x -= io.MouseDelta.x / std::max(1.0f, sw);
    app.view_.center_y -= io.MouseDelta.y / std::max(1.0f, sh);
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
  }
  else
  {
    app.view_.panning = false;
  }
}

void ViewerFrame::hover_readout()
{
  const bool show = app.hover_valid_ && !app.view_.panning && !app.view_.dragging_split && !near_divider
                    && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
  if(!show)
  {
    return;
  }
  const int px = app.hover_px_;
  const int py = app.hover_py_;
  const uint8_t* a = comp->before.px(px, py);
  const uint8_t* b = comp->after.px(px, py);
  const float de = delta_e2000(srgb8_to_lab(a[0], a[1], a[2]), srgb8_to_lab(b[0], b[1], b[2]));
  const int block = app.config_.metrics.block_size_px;
  const float inv = 1.0f / 255.0f;
  const ImVec2 swatch(style::kColorSwatchPx, style::kColorSwatchPx);
  ImGui::BeginTooltip();
  ImGui::Text("pixel %d, %d   (block %d, %d)", px, py, px / block, py / block);
  ImGui::Separator();
  ImGui::ColorButton("##a", ImVec4(a[0] * inv, a[1] * inv, a[2] * inv, 1.0f), ImGuiColorEditFlags_NoTooltip, swatch);
  ImGui::SameLine();
  ImGui::Text("before  %3d %3d %3d  #%02X%02X%02X", a[0], a[1], a[2], a[0], a[1], a[2]);
  ImGui::ColorButton("##b", ImVec4(b[0] * inv, b[1] * inv, b[2] * inv, 1.0f), ImGuiColorEditFlags_NoTooltip, swatch);
  ImGui::SameLine();
  ImGui::Text("after   %3d %3d %3d  #%02X%02X%02X", b[0], b[1], b[2], b[0], b[1], b[2]);
  ImGui::Text("dRGB %+d %+d %+d   dE2000 %.2f", b[0] - a[0], b[1] - a[1], b[2] - a[2], static_cast<double>(de));
  if(comp->heat.valid() && comp->heat.w == comp->w && comp->heat.h == comp->h)
  {
    const float v = comp->heat.at(px, py);
    ImGui::Text("%s: %.3f %s%s", metric_name(comp->heat_metric), static_cast<double>(v), metric_unit(comp->heat_metric),
                comp->heat_blocked ? " (block mean)" : "");
    ImGui::Text("colour scale: %.0f%% of %.3g", static_cast<double>(100.0f * std::clamp(v / heat_max, 0.0f, 1.0f)), static_cast<double>(heat_max));
  }
  ImGui::EndTooltip();
}

/** @brief Geometry shared by the loupe boxes of one frame. */
struct LoupeGeometry
{
  float size = 0;   /**< box edge in pixels */
  int zoom = 1;     /**< magnification */
  float uv[4] = {0, 0, 1, 1}; /**< texture window around the hovered pixel */
};

void ViewerFrame::draw_loupe_box(const LoupeGeometry& g, ImVec2 b0, ViewMode mode, const char* name)
{
  const ImVec2 b1(b0.x + g.size, b0.y + g.size);
  DrawParams d;
  fill_params(d, *comp, mode);
  d.rect[0] = b0.x;
  d.rect[1] = b0.y;
  d.rect[2] = g.size;
  d.rect[3] = g.size;
  for(int k = 0; k < 4; ++k)
  {
    d.uv[k] = g.uv[k];
  }
  d.clip[0] = b0.x;
  d.clip[1] = b0.y;
  d.clip[2] = g.size;
  d.clip[3] = g.size;
  d.nearest = true;
  d.checker = false;
  app.draws_.items.push_back(d);
  dl->AddRect(b0, b1, style::kColLoupeBorder);
  /* crosshair on the hovered pixel */
  const float cx = b0.x + g.size * 0.5f;
  const float cy = b0.y + g.size * 0.5f;
  const float s = static_cast<float>(g.zoom) * 0.5f;
  dl->AddRect(ImVec2(cx - s, cy - s), ImVec2(cx + s, cy + s), style::kColCrosshair);
  label_box(dl, ImVec2(b0.x + style::kLabelPadXPx, b0.y + style::kLabelPadXPx), name);
}

void ViewerFrame::draw_loupe()
{
  if(!app.settings_.show_loupe || !app.hover_valid_ || app.view_.panning)
  {
    return;
  }
  const LoupeConfig& lc = app.config_.view.loupe;
  const ImGuiIO& io = ImGui::GetIO();
  LoupeGeometry g;
  g.zoom = std::clamp(app.settings_.loupe_zoom, lc.min_zoom, lc.max_zoom);
  g.size = std::clamp(avail.x * lc.size_fraction, lc.min_size_px, lc.max_size_px * app.ui_scale());
  const bool show_heat = comp->heat.valid();
  const int boxes = show_heat ? 3 : 2;
  const float gap = style::kLoupeGapPx;
  const float total_w = static_cast<float>(boxes) * g.size + static_cast<float>(boxes - 1) * gap;
  const ImVec2 base(canvas.p1.x - total_w - lc.margin_px, canvas.p1.y - g.size - lc.margin_px);
  /* keep the loupe away from the cursor */
  ImVec2 origin = base;
  if(io.MousePos.x > base.x - lc.cursor_avoid_px && io.MousePos.y > base.y - lc.cursor_avoid_px)
  {
    origin = ImVec2(canvas.p0.x + lc.margin_px, base.y);
  }
  const float half_px = g.size / (2.0f * static_cast<float>(g.zoom));
  const float iw = static_cast<float>(comp->w);
  const float ih = static_cast<float>(comp->h);
  g.uv[0] = (static_cast<float>(app.hover_px_) + 0.5f - half_px) / iw;
  g.uv[1] = (static_cast<float>(app.hover_py_) + 0.5f - half_px) / ih;
  g.uv[2] = 2.0f * half_px / iw;
  g.uv[3] = 2.0f * half_px / ih;
  const ViewMode modes[3] = {ViewMode::Before, ViewMode::After, ViewMode::Heat};
  const char* names[3] = {"before", "after", "heat"};
  for(int i = 0; i < boxes; ++i)
  {
    draw_loupe_box(g, ImVec2(origin.x + static_cast<float>(i) * (g.size + gap), origin.y), modes[i], names[i]);
  }
  char zt[32];
  std::snprintf(zt, sizeof zt, "%dx", g.zoom);
  label_box(dl, ImVec2(origin.x + total_w - style::kLoupeZoomLabelOffsetXPx, origin.y + g.size - style::kLoupeZoomLabelOffsetYPx), zt);
}

void App::ui_viewer()
{
  ui_viewer_toolbar();
  ViewerFrame f(*this);
  if(!f.begin_canvas())
  {
    return;
  }
  hover_valid_ = false;
  if(!renderer_ok_)
  {
    label_box(f.dl, ImVec2(f.canvas.p0.x + style::kInsetLargePx, f.canvas.p0.y + style::kInsetLargePx),
              "OpenGL 3.3 renderer failed: " + renderer_error_);
    return;
  }
  if(f.comp == nullptr || !f.comp->ok)
  {
    f.draw_empty_state();
    return;
  }
  if(layout_ == Layout::Survey)
  {
    f.draw_survey();
    return;
  }
  f.build_panes();
  f.draw_panes();
  f.handle_mouse();
  f.hover_readout();
  f.draw_loupe();
}

} // namespace cc
