/**
 * @file ui_panels.cpp
 * @brief Menu bar, right-hand panel (heat map controls, statistics, video
 * timeline), status bar, settings and about windows.
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

namespace cc {

namespace {

/** @brief Bytes per mebibyte, for the settings sliders. */
constexpr uint64_t kBytesPerMiB = 1024ull * 1024ull;
/** @brief Slider ranges of the settings window (the config validates the same bounds). */
constexpr int kMinVideoMiB = 10;
constexpr int kMaxVideoMiB = 2000;
constexpr int kMinDurationS = 10;
constexpr int kMaxDurationS = 3600;
constexpr int kMinEdgePx = 640;
constexpr int kMaxEdgePx = 7680;
constexpr int kMinDownloadMiB = 5;
constexpr float kMinUiScale = 0.7f;
constexpr float kMaxUiScale = 2.0f;
constexpr int kBitsPerKilobit = 1000;
/** @brief Plot paddings. */
constexpr float kPsnrPlotPadDb = 1.0f;
constexpr float kSsimPlotPad = 0.02f;
constexpr double kOneSecond = 1.0;
constexpr float kLastBinFraction = 0.999f;
/** @brief Resample scales this close to 1 count as "same size". */
constexpr float kScaleTolerance = 0.001f;
/** @brief Shortest clip the timeline can show. */
constexpr double kMinTimelineS = 0.01;

/** @brief One row of the statistics table with a tooltip on both cells. */
void stat_row(const char* name, const char* value, const char* help)
{
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::TextUnformatted(name);
  HelpTooltip(help);
  ImGui::TableSetColumnIndex(1);
  ImGui::TextUnformatted(value);
  HelpTooltip(help);
}

/** @brief Draws `steps` colour swatches of a map across a rectangle. */
void draw_colormap_bar(ImDrawList* dl, ColorMap cmap, ImVec2 p0, float width, float height, int steps)
{
  for(int k = 0; k < steps; ++k)
  {
    float rgb[kColorChannels] = {0.0f, 0.0f, 0.0f};
    colormap_eval(cmap, static_cast<float>(k) / static_cast<float>(steps - 1), rgb);
    const float x0 = p0.x + static_cast<float>(k) * width / static_cast<float>(steps);
    const float x1 = p0.x + static_cast<float>(k + 1) * width / static_cast<float>(steps);
    dl->AddRectFilled(ImVec2(x0, p0.y), ImVec2(x1, p0.y + height),
                      IM_COL32(style::level(rgb[0]), style::level(rgb[1]), style::level(rgb[2]), 255));
  }
}

/** @brief "4200kb/s" or "" for a bit rate. */
std::string kbps(int64_t bit_rate)
{
  return bit_rate > 0 ? std::to_string(bit_rate / kBitsPerKilobit) + "kb/s" : std::string();
}

} // namespace

/* ---- menu bar ---------------------------------------------------------------- */

void App::ui_file_menu()
{
  if(!ImGui::BeginMenu("File"))
  {
    return;
  }
  if(ImGui::MenuItem("Open original...", "Ctrl+O", false, dialogs_.available))
  {
    std::string path;
    if(open_file_dialog(dialogs_, path, "Choose the original image or video", true))
    {
      load_slot_path(original_, path);
    }
  }
  if(ImGui::MenuItem("Paste URL into next empty slot", "Ctrl+V"))
  {
    load_from_clipboard(nullptr);
  }
  const Comparison* c = active_comparison();
  if(ImGui::MenuItem("Export heat map as PNG...", nullptr, false, c != nullptr && c->heat.valid()))
  {
    export_heat_png();
  }
  ImGui::Separator();
  if(ImGui::MenuItem("Settings...", "Ctrl+P"))
  {
    show_settings_ = true;
  }
  if(ImGui::MenuItem("Quit", "Alt+F4"))
  {
    glfwSetWindowShouldClose(window_, 1);
  }
  ImGui::EndMenu();
}

void App::ui_view_menu()
{
  if(!ImGui::BeginMenu("View"))
  {
    return;
  }
  const char* keys[kLayoutCount] = {"E", "Y / C", "Shift+Y", "Alt+Y", "Shift+Y", "N"};
  for(int i = 0; i < kLayoutCount; ++i)
  {
    const Layout l = static_cast<Layout>(i);
    const bool enabled = !(l == Layout::Survey && sites_.size() < 2);
    if(ImGui::MenuItem(layout_name(l), keys[i], layout_ == l, enabled))
    {
      set_layout(l);
    }
  }
  ImGui::Separator();
  ImGui::MenuItem("Left panel", "Tab", &show_left_panel_);
  ImGui::MenuItem("Right panel", "Tab", &show_right_panel_);
  ImGui::MenuItem("Corner loupe", "L", &settings_.show_loupe);
  if(ImGui::MenuItem("Fit", "F"))
  {
    view_.reset_zoom();
  }
  if(ImGui::MenuItem("100 %", "Z"))
  {
    view_.zoom = 1.0f;
  }
  if(ImGui::MenuItem("200 %", nullptr))
  {
    view_.zoom = 2.0f;
  }
  ImGui::EndMenu();
}

void App::ui_menubar()
{
  if(!ImGui::BeginMainMenuBar())
  {
    return;
  }
  ui_file_menu();
  ui_view_menu();
  if(ImGui::BeginMenu("Help"))
  {
    if(ImGui::MenuItem("Shortcuts & about", "F1"))
    {
      show_about_ = true;
    }
    ImGui::EndMenu();
  }
  /* right side: worker status */
  const std::string label = worker_.busy() ? worker_.current_label() : "";
  if(!label.empty())
  {
    const float f = worker_.progress().fraction.load();
    char buf[style::kLabelBufChars];
    std::snprintf(buf, sizeof buf, "%s  %3.0f%%", label.c_str(), static_cast<double>(f) * 100.0);
    const float w = ImGui::CalcTextSize(buf).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - w - style::kMenuRightMarginPx);
    ImGui::TextDisabled("%s", buf);
  }
  ImGui::EndMainMenuBar();
}

void App::ui_status_bar()
{
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float h = ImGui::GetFrameHeight() + style::kStatusExtraHeightPx;
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - h));
  ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, h));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style::kStatusPaddingXPx, style::kStatusPaddingYPx));
  ImGui::Begin("##status", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
                   | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
  const Comparison* c = active_comparison();
  if(ImGui::GetTime() < toast_until_ && !toast_.empty())
  {
    ImGui::TextColored(style::kColToast, "%s", toast_.c_str());
  }
  else if(!ffmpeg_.video_available())
  {
    ImGui::TextDisabled("no video back-end - videos are disabled (see Settings)");
  }
  else if(c != nullptr && c->ok)
  {
    Slot* s = active_site();
    const char* scaled = c->resample_scale < 1.0f - kScaleTolerance ? "  (original downscaled)"
                                                                     : (c->resample_scale > 1.0f + kScaleTolerance ? "  (site version upscaled)" : "");
    ImGui::TextDisabled("%s: compared at %dx%d%s%s", s != nullptr ? s->site_label(config_.sites).c_str() : "", c->w, c->h,
                        c->aspect_mismatch ? "  (aspect ratio differs: original centre-cropped to match)" : "", scaled);
  }
  else
  {
    ImGui::TextDisabled("drop files anywhere, paste URLs with Ctrl+V, F1 for shortcuts");
  }
  if(settings_.show_fps)
  {
    char buf[style::kLineBufChars];
    std::snprintf(buf, sizeof buf, "%.0f fps  %s", static_cast<double>(ImGui::GetIO().Framerate), renderer_.gl_info().c_str());
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(buf).x - style::kMenuRightMarginPx);
    ImGui::TextDisabled("%s", buf);
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
}

/* ---- right panel ------------------------------------------------------------- */

void App::ui_right_panel()
{
  Comparison* comp = active_comparison();
  if(ImGui::CollapsingHeader("Heat map", ImGuiTreeNodeFlags_DefaultOpen))
  {
    ui_heat_panel();
  }
  if(ImGui::CollapsingHeader("Statistics", ImGuiTreeNodeFlags_DefaultOpen))
  {
    ui_stats_panel(comp);
  }
  const bool any_video = original_.media.is_video() || std::any_of(sites_.begin(), sites_.end(), [](const Slot& s) { return s.media.is_video(); });
  if(any_video && ImGui::CollapsingHeader("Video", ImGuiTreeNodeFlags_DefaultOpen))
  {
    ui_video_panel();
  }
}

void App::ui_heat_metric_and_mode()
{
  const float w = ImGui::GetContentRegionAvail().x;
  ImGui::SetNextItemWidth(w);
  if(ImGui::BeginCombo("##metric", metric_name(heat_.metric)))
  {
    for(int i = 0; i < kMetricCount; ++i)
    {
      const Metric m = static_cast<Metric>(i);
      if(ImGui::Selectable(metric_name(m), heat_.metric == m))
      {
        heat_.metric = m;
      }
      HelpTooltip(metric_help(m));
    }
    ImGui::EndCombo();
  }
  if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
  {
    ImGui::SetTooltip("%s\n\n(M cycles metrics)", metric_help(heat_.metric));
  }
  /* what the "after" side shows */
  const char* modes[] = {"site version only", "heat map only", "heat map overlay"};
  int mode_idx = heat_.after_mode == ViewMode::After ? 0 : (heat_.after_mode == ViewMode::Heat ? 1 : 2);
  ImGui::SetNextItemWidth(w);
  if(ImGui::Combo("##aftermode", &mode_idx, modes, 3))
  {
    heat_.after_mode = mode_idx == 0 ? ViewMode::After : (mode_idx == 1 ? ViewMode::Heat : ViewMode::Overlay);
  }
  HelpTooltip("What the 'After' side of the compare view shows (H toggles the overlay, Shift+H the plain heat map)");
}

void App::ui_heat_overlay_controls()
{
  const float w = ImGui::GetContentRegionAvail().x;
  ImGui::SetNextItemWidth(w);
  ImGui::SliderFloat("##intensity", &heat_.intensity, 0.0f, 1.0f, "overlay intensity %.2f");
  HelpTooltip("Opacity of the heat map over the image. 0 = image only, 1 = heat map only.\n[ and ] nudge it (heatmap.intensity_step).");
  ImGui::Checkbox("opacity follows error", &heat_.proportional);
  HelpTooltip("On: barely-changed pixels stay transparent so the photo shows through; only real damage lights up.\n"
              "Off: a constant tint everywhere (classic false-colour overlay).");
  ImGui::SameLine();
  ImGui::TextDisabled("over:");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
  ImGui::Combo("##base", &heat_.overlay_base, "original\0site version\0");
  HelpTooltip("Which image sits under the overlay");
}

void App::ui_colormap_combo()
{
  const HeatMapConfig& hc = config_.heatmap;
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
  if(!ImGui::BeginCombo("##cmap", colormap_name(heat_.cmap)))
  {
    HelpTooltip("Colour map. Inferno/Viridis are perceptually uniform; 'Red alert' is best as an overlay on colourful photos.");
    return;
  }
  for(int i = 0; i < kColorMapCount; ++i)
  {
    const ColorMap cm = static_cast<ColorMap>(i);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool sel = ImGui::Selectable(colormap_name(cm), heat_.cmap == cm);
    const float sw = hc.colormap_preview_width_px;
    const float sh = ImGui::GetTextLineHeight() - style::kSwatchHeightReducePx;
    const float x0 = p.x + ImGui::GetContentRegionAvail().x - sw;
    draw_colormap_bar(ImGui::GetWindowDrawList(), cm, ImVec2(x0, p.y + style::kSwatchInsetPx), sw, sh, hc.colormap_preview_steps);
    if(sel)
    {
      heat_.cmap = cm;
      renderer_.set_colormap(heat_.cmap);
    }
  }
  ImGui::EndCombo();
}

void App::ui_heat_scale_controls()
{
  const HeatMapConfig& hc = config_.heatmap;
  const float w = ImGui::GetContentRegionAvail().x;
  char auto_label[64];
  std::snprintf(auto_label, sizeof auto_label, "auto scale (%dth percentile)", config_.metrics.auto_scale_percentile);
  ImGui::Checkbox(auto_label, &heat_.auto_scale);
  HelpTooltip("On: the brightest colour is assigned to a high percentile of the map, so a few extreme pixels don't wash everything out.\n"
              "Off: set the maximum yourself to compare sites on the same scale.");
  if(!heat_.auto_scale)
  {
    ImGui::SetNextItemWidth(w);
    const float def = config_.metrics.default_scale[static_cast<int>(heat_.metric)];
    ImGui::SliderFloat("##max", &heat_.manual_max, def * hc.manual_max_min_factor, def * hc.manual_max_max_factor, "max = %.3g",
                       ImGuiSliderFlags_Logarithmic);
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
      ImGui::SetTooltip("Value mapped to the top of the colour scale (%s)", metric_unit(heat_.metric));
    }
  }
  ImGui::SetNextItemWidth(w * style::kHalfGammaWidth);
  ImGui::SliderFloat("##gamma", &heat_.gamma, hc.gamma_min, hc.gamma_max, "gamma %.2f");
  HelpTooltip("< 1 boosts faint differences, > 1 suppresses them");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
  ImGui::SliderFloat("##thr", &heat_.threshold, 0.0f, 1.0f, "cut-off %.2f");
  HelpTooltip("Hide everything below this fraction of the scale (overlay/heat map only)");
  char block_label[64];
  std::snprintf(block_label, sizeof block_label, "%dx%d block view", config_.metrics.block_size_px, config_.metrics.block_size_px);
  ImGui::Checkbox(block_label, &heat_.block_view);
  HelpTooltip("Average the map over the JPEG block grid: shows which blocks the encoder sacrificed (B toggles)");
}

void App::ui_heat_legend()
{
  const HeatMapConfig& hc = config_.heatmap;
  const Comparison* comp = active_comparison();
  const float vmax = heat_scale_max(comp);
  const float w = ImGui::GetContentRegionAvail().x;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float lh = hc.legend_height_px;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  draw_colormap_bar(dl, heat_.cmap, p, w, lh, hc.legend_swatches);
  if(heat_.threshold > 0.0f)
  {
    const float x = p.x + heat_.threshold * w;
    dl->AddLine(ImVec2(x, p.y - style::kLabelPadYPx), ImVec2(x, p.y + lh + style::kLabelPadYPx), style::kColThreshold, style::kDividerWidthNearPx);
  }
  ImGui::InvisibleButton("legend", ImVec2(w, lh));
  if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
  {
    const float mx = ImGui::GetIO().MousePos.x;
    const float t = std::clamp((mx - p.x) / w, 0.0f, 1.0f);
    const float v = std::pow(t, 1.0f / clamped_gamma()) * vmax;
    ImGui::SetTooltip("%.3g %s at this colour\nscale 0 .. %.3g %s", static_cast<double>(v), metric_unit(heat_.metric), static_cast<double>(vmax),
                      metric_unit(heat_.metric));
  }
  char hi[style::kNumberBufChars];
  std::snprintf(hi, sizeof hi, "%.3g %s", static_cast<double>(vmax), metric_unit(heat_.metric));
  ImGui::TextDisabled("0");
  ImGui::SameLine(w - ImGui::CalcTextSize(hi).x);
  ImGui::TextDisabled("%s", hi);
  if(comp != nullptr && comp->heat_pending)
  {
    ImGui::TextDisabled("computing %s...", metric_name(heat_.metric));
  }
  if(ImGui::SmallButton("export PNG"))
  {
    export_heat_png();
  }
  HelpTooltip("Save the colour-mapped heat map of the active site as a PNG");
}

void App::ui_heat_panel()
{
  ui_heat_metric_and_mode();
  if(heat_.after_mode == ViewMode::Overlay)
  {
    ui_heat_overlay_controls();
  }
  ui_colormap_combo();
  ui_heat_scale_controls();
  ui_heat_legend();
}

/* ---- statistics -------------------------------------------------------------- */

void App::ui_stats_file_rows(const Slot& site)
{
  char buf[256];
  std::snprintf(buf, sizeof buf, "%dx%d -> %dx%d", original_.media.native_w, original_.media.native_h, site.media.native_w, site.media.native_h);
  stat_row("size", buf, "Pixel dimensions: original -> what the site served. Most sites cap the long edge (1080, 2048, 4096...).");
  if(original_.media.bytes_size != 0 && site.media.bytes_size != 0)
  {
    std::snprintf(buf, sizeof buf, "%s -> %s (%.0f%%)", format_bytes(original_.media.bytes_size).c_str(), format_bytes(site.media.bytes_size).c_str(),
                  100.0 * static_cast<double>(site.media.bytes_size) / static_cast<double>(original_.media.bytes_size));
    stat_row("file size", buf, "Bytes on disk: original -> site version.");
  }
}

void App::ui_stats_still_rows(const Slot& site)
{
  char buf[256];
  const std::string fmt = std::string(file_kind_name(original_.media.kind)) + " -> " + file_kind_name(site.media.kind);
  stat_row("format", fmt.c_str(), "Container/codec of the two files (detected from the bytes, not the file name).");
  if(site.media.kind != FileKind::JPEG || !site.media.jpeg.ok)
  {
    return;
  }
  const JpegInfo& j = site.media.jpeg;
  std::snprintf(buf, sizeof buf, "q~%d, %s, %s%s", j.quality, j.subsampling.c_str(), j.progressive ? "progressive" : "baseline",
                j.table_fit_error > config_.metrics.custom_table_fit_error_pct ? ", custom tables" : "");
  stat_row("site JPEG", buf,
           "Estimated IJG quality from the quantisation tables (exact when the encoder uses the standard\n"
           "libjpeg tables; approximate for mozjpeg/Photoshop-style tables), chroma subsampling and scan type.\n"
           "4:2:0 halves the colour resolution - look at red text and saturated edges.");
  if(original_.media.kind == FileKind::JPEG && original_.media.jpeg.ok)
  {
    std::snprintf(buf, sizeof buf, "q~%d, %s", original_.media.jpeg.quality, original_.media.jpeg.subsampling.c_str());
    stat_row("original JPEG", buf, "Your upload was already a JPEG with these settings; the site re-encoded it a second time.");
  }
  const std::string meta = std::string(j.has_exif ? "EXIF " : "") + (j.has_icc ? "ICC " : "");
  stat_row("metadata kept", meta.empty() ? "none (stripped)" : meta.c_str(), "Whether EXIF and an ICC colour profile survived the upload.");
}

void App::ui_stats_video_rows(const Slot& site, const Comparison& comp)
{
  char buf[256];
  const VideoInfo& a = original_.media.video;
  const VideoInfo& b = site.media.video;
  std::snprintf(buf, sizeof buf, "%s %s -> %s %s", a.codec.c_str(), kbps(a.bit_rate).c_str(), b.codec.c_str(), kbps(b.bit_rate).c_str());
  stat_row("video", buf, "Codec and bitrate reported by ffprobe for the original -> site version.");
  std::snprintf(buf, sizeof buf, "%.3g -> %.3g fps", a.fps, b.fps);
  stat_row("frame rate", buf, "Sites often cap frame rate (30 fps) or drop frames.");
  std::snprintf(buf, sizeof buf, "%.2f s / %.2f s", comp.frame_time_before, comp.frame_time_after);
  stat_row("frames compared", buf, "Timestamps of the two frames being compared.");
}

void App::ui_stats_quality_rows(const PairStats& s)
{
  char buf[256];
  std::snprintf(buf, sizeof buf, "%.2f dB", s.psnr);
  stat_row("PSNR", buf,
           "Peak signal-to-noise ratio over RGB. > 45 dB: visually lossless; 35-45: good; 30-35: visible on close inspection;\n"
           "< 30: obvious artefacts. (MSE from CLIJ2-style squared difference + mean.)");
  std::snprintf(buf, sizeof buf, "%.4f", s.ssim);
  stat_row("SSIM", buf,
           "Mean structural similarity (luma, Gaussian window). 1 = identical structure; > 0.98 excellent; 0.95 good;\n"
           "< 0.90 clearly degraded textures/edges.");
  std::snprintf(buf, sizeof buf, "%.2f mean / %.1f max", s.mean_de2000, s.max_de2000);
  stat_row("dE2000", buf, "CIEDE2000 colour difference per pixel. ~1 is just noticeable; mean < 1 is essentially invisible.");
  std::snprintf(buf, sizeof buf, "%.1f%% of pixels", s.pct_over_jnd);
  char help[style::kLabelBufChars];
  std::snprintf(help, sizeof help, "Share of pixels whose colour moved by more than a just-noticeable difference (dE2000 > %.1f).",
                static_cast<double>(config_.metrics.jnd_delta_e2000));
  stat_row("visible changes", buf, help);
  std::snprintf(buf, sizeof buf, "%.1f%%", s.pct_identical);
  stat_row("untouched pixels", buf, "Pixels that came back bit-identical. High for PNG/lossless paths, ~0 for JPEG re-encodes or any resize.");
  std::snprintf(buf, sizeof buf, "%.2f levels", s.mean_abs);
  stat_row("mean abs diff", buf, "Average of the largest per-channel change (0-255 levels).");
}

void App::ui_error_histogram(const Comparison& comp)
{
  /* error histogram (log scale like darktable's histogram toggle) */
  std::vector<float> h(comp.heat.hist.size());
  float mx = 1.0f;
  for(std::size_t i = 0; i < h.size(); ++i)
  {
    const float count = static_cast<float>(comp.heat.hist[i]);
    h[i] = heat_.log_histogram ? std::log1p(count) : count;
    mx = std::max(mx, h[i]);
  }
  char overlay[style::kLineBufChars];
  std::snprintf(overlay, sizeof overlay, "%s: median %.2f  p95 %.2f  p99 %.2f", metric_unit(comp.heat_metric), static_cast<double>(comp.heat.p50),
                static_cast<double>(comp.heat.p95), static_cast<double>(comp.heat.p99));
  ImGui::PlotHistogram("##hist", h.data(), static_cast<int>(h.size()), 0, overlay, 0.0f, mx,
                       ImVec2(ImGui::GetContentRegionAvail().x, config_.app.panels.histogram_height_px));
  if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !h.empty())
  {
    const float t = std::clamp((ImGui::GetIO().MousePos.x - ImGui::GetItemRectMin().x) / std::max(1.0f, ImGui::GetItemRectSize().x), 0.0f,
                               kLastBinFraction);
    const std::size_t bin = static_cast<std::size_t>(t * static_cast<float>(h.size()));
    const float bins = static_cast<float>(h.size());
    ImGui::SetTooltip("%s %.3g .. %.3g: %u pixels\n(distribution of the heat map values, %s scale)", metric_unit(comp.heat_metric),
                      static_cast<double>(comp.heat.max * static_cast<float>(bin) / bins),
                      static_cast<double>(comp.heat.max * static_cast<float>(bin + 1) / bins), comp.heat.hist[bin],
                      heat_.log_histogram ? "log" : "linear");
  }
  ImGui::Checkbox("log", &heat_.log_histogram);
  ImGui::SameLine();
  ImGui::TextDisabled("max %.3g, mean %.3g", static_cast<double>(comp.heat.max), static_cast<double>(comp.heat.mean));
}

void App::ui_alignment_controls(Comparison& comp)
{
  ImGui::Spacing();
  ImGui::TextWrapped("The site changed the aspect ratio (crop). The original was scaled to cover and centre-cropped; nudge the alignment if edges look off:");
  ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
  const struct
  {
    const char* id;
    ImGuiDir dir;
    int dx;
    int dy;
  } arrows[] = {{"al", ImGuiDir_Left, -1, 0}, {"ar", ImGuiDir_Right, 1, 0}, {"au", ImGuiDir_Up, 0, -1}, {"ad", ImGuiDir_Down, 0, 1}};
  for(const auto& a : arrows)
  {
    if(ImGui::ArrowButton(a.id, a.dir))
    {
      comp.align_dx += a.dx;
      comp.align_dy += a.dy;
      comp.align_dirty = true;
    }
    ImGui::SameLine();
  }
  ImGui::PopItemFlag();
  ImGui::Text("offset %d, %d px", comp.align_dx, comp.align_dy);
  HelpTooltip("Hold the arrows to slide the crop window; the comparison is recomputed when you release.");
  if(comp.align_dirty && !ImGui::IsAnyMouseDown() && !worker_.busy())
  {
    comp.align_dirty = false;
    for(int i = 0; i < static_cast<int>(sites_.size()); ++i)
    {
      if(sites_[static_cast<std::size_t>(i)].id == comp.slot_id)
      {
        visualize(i);
      }
    }
  }
}

void App::ui_stats_panel(Comparison* comp)
{
  const Slot* site = active_site();
  if(comp == nullptr || !comp->ok || site == nullptr)
  {
    ImGui::TextDisabled("no comparison yet");
    return;
  }
  if(ImGui::BeginTable("stats", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
  {
    ui_stats_file_rows(*site);
    if(site->media.is_video())
    {
      ui_stats_video_rows(*site, *comp);
    }
    else
    {
      ui_stats_still_rows(*site);
    }
    ui_stats_quality_rows(comp->stats);
    ImGui::EndTable();
  }
  if(comp->heat.valid())
  {
    ui_error_histogram(*comp);
  }
  if(comp->aspect_mismatch)
  {
    ui_alignment_controls(*comp);
  }
}

/* ---- video ------------------------------------------------------------------- */

void App::ui_video_timeline(const MediaSource& ref)
{
  const double dur = std::max(kMinTimelineS, ref.video.duration_s);
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
  /* video_scrub_ keeps the value while dragging; it follows video_time_ otherwise */
  ImGui::SliderFloat("##time", &video_scrub_, 0.0f, static_cast<float>(dur), "%.2f s");
  HelpTooltip("Timestamp to compare. Frames are pulled with ffmpeg when you release the slider.\n"
              "ffmpeg -ss T -i file -frames:v 1 -f rawvideo -pix_fmt rgb24 -");
  if(ImGui::IsItemDeactivatedAfterEdit())
  {
    seek_all(static_cast<double>(video_scrub_));
  }
  else if(!ImGui::IsItemActive())
  {
    video_scrub_ = static_cast<float>(video_time_);
  }
  const double step = ref.video.fps > 0 ? 1.0 / ref.video.fps : config_.ffmpeg.fallback_frame_step_s;
  if(ImGui::Button("<< 1 s"))
  {
    seek_all(std::max(0.0, video_time_ - kOneSecond));
  }
  ImGui::SameLine();
  if(ImGui::Button("< frame"))
  {
    seek_all(std::max(0.0, video_time_ - step));
  }
  ImGui::SameLine();
  if(ImGui::Button("frame >"))
  {
    seek_all(std::min(dur, video_time_ + step));
  }
  ImGui::SameLine();
  if(ImGui::Button("1 s >>"))
  {
    seek_all(std::min(dur, video_time_ + kOneSecond));
  }
  ImGui::Text("%s", ref.video.summary().c_str());
  if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
  {
    ImGui::SetTooltip("Reported by ffprobe. Limits (Settings): %s, %.0f s, %d px per edge.", format_bytes(settings_.video.max_bytes).c_str(),
                      settings_.video.max_duration_s, settings_.video.max_edge_px);
  }
}

void App::start_video_sampling()
{
  const Slot* site = active_site();
  CC_REQUIRE(site != nullptr && site->media.is_video() && original_.media.is_video(), return);
  sampling_ = true;
  sample_psnr_.clear();
  sample_ssim_.clear();
  const VideoInfo a = original_.media.video;
  const VideoInfo b = site->media.video;
  const FfmpegTools tools = ffmpeg_;
  const int n = std::clamp(sample_count_, config_.ffmpeg.sample_points_min, config_.ffmpeg.sample_points_max);
  const bool at_site = settings_.compare_at_site_resolution;
  const MetricsConfig metrics = config_.metrics;
  const ThreadingConfig threading = config_.threading;
  worker_.submit("sampling video", [this, a, b, tools, n, at_site, metrics, threading](Progress& progress) -> std::function<void()> {
    std::vector<float> psnr;
    std::vector<float> ssim;
    for(int i = 0; i < n && !progress.cancel; ++i)
    {
      const double ta = a.duration_s * (i + 0.5) / n;
      const double tb = b.duration_s * (i + 0.5) / n; /* same relative position (sites sometimes trim) */
      Image fa;
      Image fb;
      std::string err;
      const int w = at_site ? b.width : a.width;
      const int h = at_site ? b.height : a.height;
      if(!extract_frame(tools, a, ta, fa, err, w, h) || !extract_frame(tools, b, tb, fb, err, w, h))
      {
        break;
      }
      const PairStats st = compute_stats(fa, fb, metrics, threading, nullptr);
      psnr.push_back(static_cast<float>(st.psnr));
      ssim.push_back(static_cast<float>(st.ssim));
      progress.set(static_cast<float>(i + 1) / static_cast<float>(n), "sampling " + std::to_string(i + 1) + "/" + std::to_string(n));
    }
    return [this, psnr, ssim]() {
      sample_psnr_ = psnr;
      sample_ssim_ = ssim;
      sampling_ = false;
    };
  });
}

void App::ui_video_sampling(const MediaSource& /*ref*/)
{
  const Slot* site = active_site();
  ImGui::SetNextItemWidth(style::kSamplePointsWidthPx * ui_scale());
  ImGui::SliderInt("##n", &sample_count_, config_.ffmpeg.sample_points_min, config_.ffmpeg.sample_points_max, "%d points");
  ImGui::SameLine();
  const bool can_sample = !sampling_ && site != nullptr && site->media.is_video() && original_.media.is_video();
  ImGui::BeginDisabled(!can_sample);
  if(ImGui::Button("Sample quality over time"))
  {
    start_video_sampling();
  }
  ImGui::EndDisabled();
  if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
  {
    ImGui::SetTooltip("Extract N evenly spaced frames from both videos and plot PSNR / SSIM over time (both slots must be videos).");
  }
}

void App::ui_video_sample_plots(double duration)
{
  if(sample_psnr_.empty() || sample_ssim_.size() != sample_psnr_.size())
  {
    return;
  }
  const float plot_h = config_.app.panels.plot_height_px;
  const auto minmax_p = std::minmax_element(sample_psnr_.begin(), sample_psnr_.end());
  char ov[64];
  std::snprintf(ov, sizeof ov, "PSNR %.1f .. %.1f dB", static_cast<double>(*minmax_p.first), static_cast<double>(*minmax_p.second));
  ImGui::PlotLines("##psnr", sample_psnr_.data(), static_cast<int>(sample_psnr_.size()), 0, ov, std::floor(*minmax_p.first - kPsnrPlotPadDb),
                   std::ceil(*minmax_p.second + kPsnrPlotPadDb), ImVec2(ImGui::GetContentRegionAvail().x, plot_h));
  if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
  {
    const float t = std::clamp((ImGui::GetIO().MousePos.x - ImGui::GetItemRectMin().x) / std::max(1.0f, ImGui::GetItemRectSize().x), 0.0f,
                               kLastBinFraction);
    const std::size_t i = static_cast<std::size_t>(t * static_cast<float>(sample_psnr_.size()));
    ImGui::SetTooltip("sample %zu: PSNR %.2f dB, SSIM %.4f  (click to jump there)", i + 1, static_cast<double>(sample_psnr_[i]),
                      static_cast<double>(sample_ssim_[i]));
    if(ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
      seek_all(duration * (static_cast<double>(i) + 0.5) / static_cast<double>(sample_psnr_.size()));
    }
  }
  const auto minmax_s = std::minmax_element(sample_ssim_.begin(), sample_ssim_.end());
  std::snprintf(ov, sizeof ov, "SSIM %.3f .. %.3f", static_cast<double>(*minmax_s.first), static_cast<double>(*minmax_s.second));
  ImGui::PlotLines("##ssim", sample_ssim_.data(), static_cast<int>(sample_ssim_.size()), 0, ov, std::max(0.0f, *minmax_s.first - kSsimPlotPad),
                   std::min(1.0f, *minmax_s.second + kSsimPlotPad), ImVec2(ImGui::GetContentRegionAvail().x, plot_h));
}

void App::ui_video_panel()
{
  const Slot* site = active_site();
  const MediaSource* ref = nullptr;
  if(original_.media.is_video())
  {
    ref = &original_.media;
  }
  else if(site != nullptr && site->media.is_video())
  {
    ref = &site->media;
  }
  else
  {
    for(const Slot& s : sites_)
    {
      if(s.media.is_video())
      {
        ref = &s.media;
        break;
      }
    }
  }
  if(ref == nullptr)
  {
    ImGui::TextDisabled("no video loaded");
    return;
  }
  ui_video_timeline(*ref);
  ui_video_sampling(*ref);
  ui_video_sample_plots(std::max(kMinTimelineS, ref->video.duration_s));
}

/* ---- settings and about ------------------------------------------------------ */

void App::ui_settings_video()
{
  ImGui::SeparatorText("Video limits");
  int mb = static_cast<int>(settings_.video.max_bytes / kBytesPerMiB);
  if(ImGui::SliderInt("max file size (MB)", &mb, kMinVideoMiB, kMaxVideoMiB, "%d MB", ImGuiSliderFlags_Logarithmic))
  {
    settings_.video.max_bytes = static_cast<uint64_t>(mb) * kBytesPerMiB;
  }
  HelpTooltip("Videos above this size are refused before they are opened. Frames are decoded one at a time so RAM stays low either way.");
  int secs = static_cast<int>(settings_.video.max_duration_s);
  if(ImGui::SliderInt("max duration (s)", &secs, kMinDurationS, kMaxDurationS, "%d s", ImGuiSliderFlags_Logarithmic))
  {
    settings_.video.max_duration_s = secs;
  }
  ImGui::SliderInt("max long edge (px)", &settings_.video.max_edge_px, kMinEdgePx, kMaxEdgePx);
  HelpTooltip("Videos whose longer side exceeds this are refused (portrait and landscape alike).");
  ImGui::TextWrapped("ffmpeg: %s", ffmpeg_.available() ? ffmpeg_.version.c_str() : "not found (optional)");
  HelpTooltip("Optional. MP4 and MOV open through OpenCV (AVFoundation on macOS); the ffmpeg executable is only needed for\n"
              "WebM, MKV, FLV and other containers the system cannot read, and for AVIF/HEIC on Linux.\n"
              "Looked up next to the executable, in /usr/local/bin and /opt/local/bin (MacPorts), then on PATH.\n"
              "macOS 12: sudo port install ffmpeg, or a static build from evermeet.cx/ffmpeg");
  if(!ffmpeg_.available() && ImGui::Button("re-detect ffmpeg"))
  {
    ffmpeg_ = locate_ffmpeg(config_.ffmpeg, config_.process, config_.acceleration.video_backend);
  }
  ImGui::SeparatorText("Downloads");
  int dl_mb = static_cast<int>(settings_.max_download_bytes / kBytesPerMiB);
  if(ImGui::SliderInt("max download (MB)", &dl_mb, kMinDownloadMiB, kMaxVideoMiB, "%d MB", ImGuiSliderFlags_Logarithmic))
  {
    settings_.max_download_bytes = static_cast<uint64_t>(dl_mb) * kBytesPerMiB;
  }
}

void App::ui_settings_acceleration()
{
  ImGui::SeparatorText("Acceleration (OpenCV / OpenCL)");
  const AccelStatus& st = acceleration_status();
  ImGui::TextWrapped("OpenCV %s, %d CPU threads (%s)", st.opencv_version.c_str(), st.cpu_threads, st.cpu_features.c_str());
  ImGui::TextWrapped("OpenCL device: %s", st.device.empty() ? "none" : st.device.c_str());
  ImGui::BeginDisabled(!st.opencl_ok);
  bool gpu = opencl_enabled();
  if(ImGui::Checkbox("use the GPU (OpenCL) for heat maps, statistics and resampling", &gpu))
  {
    gpu = set_opencl_enabled(gpu);
    toast(gpu ? "OpenCL on: new heat maps are computed on the GPU" : "OpenCL off: new heat maps are computed on the CPU", config_.app.toast.short_s);
  }
  ImGui::EndDisabled();
  if(!st.opencl_ok)
  {
    ImGui::TextDisabled("OpenCL unavailable: %s", st.reason.c_str());
  }
  HelpTooltip("The same computation runs either way (the CPU path is OpenCV's SIMD code on all cores).\n"
              "In auto mode OpenCL is only switched on after a start-up self-test compares the GPU and CPU results.\n"
              "Images below acceleration.min_gpu_pixels stay on the CPU. Settings: acceleration.* in config.yaml.");
  ImGui::TextWrapped("Video: %s    Stills: %s", ffmpeg_.use_opencv() ? st.video_io.c_str() : "ffmpeg executable", st.still_decoders.c_str());
}

void App::ui_settings_display()
{
  ImGui::SeparatorText("Display & power");
  ImGui::SliderFloat("UI scale", &settings_.ui_scale, kMinUiScale, kMaxUiScale, "%.2f");
  if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
  {
    ImGui::SetTooltip("Multiplied with the monitor DPI scale (%.2f). Applies to panels on the next frame; fonts follow.",
                      static_cast<double>(dpi_scale_));
  }
  if(ImGui::IsItemDeactivatedAfterEdit())
  {
#if IMGUI_VERSION_NUM >= 19200
    ImGui::GetStyle().FontScaleDpi = ui_scale();
#else
    ImGui::GetIO().FontGlobalScale = ui_scale();
#endif
  }
  ImGui::Checkbox("power saver (idle at a few fps)", &settings_.power_saver);
  HelpTooltip("Sleeps between frames when nothing is happening so the handheld's GPU/CPU stay near idle. Off = always fast frames.");
  ImGui::Checkbox("show fps / GPU", &settings_.show_fps);
  ImGui::SliderInt("loupe zoom", &settings_.loupe_zoom, config_.view.loupe.min_zoom, config_.view.loupe.max_zoom, "%dx");
  ImGui::Checkbox("auto visualize", &settings_.auto_visualize);
  ImGui::TextDisabled("renderer: %s", renderer_.gl_info().c_str());
  ImGui::TextDisabled("worker threads: %d (OpenCV pool)", acceleration_status().cpu_threads);
  const std::string user_cfg = user_config_path();
  ImGui::TextDisabled("settings file: %s", user_cfg.empty() ? "config.yaml next to the executable" : user_cfg.c_str());
  if(!user_cfg.empty() && ImGui::Button("edit config.yaml..."))
  {
    std::string path;
    std::string err;
    if(open_user_config(config_.process, path, err))
    {
      toast("Opened " + path + " - changes apply at the next start", config_.app.toast.long_s);
    }
    else
    {
      toast(err, config_.app.toast.long_s);
    }
  }
  HelpTooltip("Creates the file from the built-in defaults if needed and opens it in the text editor.\n"
              "See docs/how-to/change-config.md for every key.");
}

void App::ui_settings_window()
{
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_FirstUseEver,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(config_.app.panels.settings_window_width_px * ui_scale(), 0), ImGuiCond_FirstUseEver);
  if(!ImGui::Begin("Settings", &show_settings_, ImGuiWindowFlags_NoCollapse))
  {
    ImGui::End();
    return;
  }
  ui_settings_acceleration();
  ui_settings_video();
  ui_settings_display();
  ImGui::End();
}

void App::ui_about_window()
{
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_FirstUseEver,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(config_.app.panels.about_window_width_px * ui_scale(), 0), ImGuiCond_FirstUseEver);
  if(!ImGui::Begin("Shortcuts & about", &show_about_, ImGuiWindowFlags_NoCollapse))
  {
    ImGui::End();
    return;
  }
  ImGui::TextWrapped("%s - see how a site recompressed your image or video: load the original and the copy you downloaded back, "
                     "then look at the heat map.",
                     config_.app.name.c_str());
  ImGui::SeparatorText("Lightroom-style views");
  ImGui::BulletText("Y  Before/After Left/Right      Alt+Y  Top/Bottom      Shift+Y  Split variant");
  ImGui::BulletText("\\  toggle Before/After (Loupe) or swap sides (split)      Space (hold)  peek at Before");
  ImGui::BulletText("E  Loupe      C  Compare (Left/Right)      N  Survey (all sites)      Change view button cycles");
  ImGui::BulletText("Left / Right arrows  previous / next site      Tab  hide/show panels");
  ImGui::SeparatorText("Zoom & pan");
  ImGui::BulletText("Mouse wheel  zoom about the cursor (snaps to fit / 100%% / 200%%)");
  ImGui::BulletText("Z or middle click  fit -> 100%% -> 200%%      F  fit      Ctrl + / Ctrl -  step      drag  pan");
  ImGui::BulletText("Split view: drag the divider, click its handle to rotate, double-click to centre");
  ImGui::SeparatorText("Heat map");
  ImGui::BulletText("H  overlay on/off      Shift+H  heat map only      [ ]  overlay intensity      M  next metric      B  block view");
  ImGui::BulletText("L  corner loupe      hover the image, legend, stats and site names for details");
  ImGui::SeparatorText("Files");
  ImGui::BulletText("Drop files on a slot (or anywhere), Ctrl+V pastes a URL or path, Ctrl+O opens a file into the next empty slot");
  ImGui::SeparatorText("Credits");
  ImGui::TextWrapped("Kernels ported from CLIJ2 / clEsperanto (BSD-3, Haase et al. 2020). UI behaviour modelled on darktable's snapshots, culling and "
                     "thumbnail overlays (GPL-3, concepts only; no code copied) and Adobe Lightroom Classic's compare views. OpenCV (Apache-2.0) with its "
                     "bundled libjpeg-turbo (BSD/IJG), libpng (libpng), libwebp (BSD), libtiff (libtiff) and zlib (zlib); Dear ImGui (MIT), GLFW (zlib), "
                     "libcurl (MIT-style), yaml-cpp (MIT); ffmpeg (LGPL/GPL, optional external executable). On macOS: ImageIO, AVFoundation and "
                     "OpenCL system frameworks.");
  ImGui::End();
}

} // namespace cc
