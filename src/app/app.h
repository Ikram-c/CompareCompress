/**
 * @file app.h
 * @brief Application state and orchestration.
 *
 * UI code lives in ui_*.cpp; heavy work runs on the BackgroundWorker and
 * results are applied on the main thread (which owns the OpenGL context).
 * Every tunable number comes from the Config the App is constructed with.
 */
#pragma once

#include "app/fuzzy_combo.h"
#include "core/colormap.h"
#include "core/config.h"
#include "core/ffmpeg.h"
#include "core/image.h"
#include "core/jpeg_info.h"
#include "core/metrics.h"
#include "core/threading.h"
#include "gpu/renderer.h"
#include "platform/dialogs.h"

#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace cc {

/** @brief Settings the user can change at run time (initialised from config.yaml). */
struct Settings
{
  VideoLimits video;
  uint64_t max_download_bytes = 0;
  uint64_t max_image_bytes = 0;
  float ui_scale = 1.0f;   /**< multiplied with the monitor DPI scale */
  bool power_saver = true; /**< idle at a few fps */
  bool show_loupe = true;
  int loupe_zoom = 1; /**< magnification in the corner loupe */
  bool show_fps = false;
  bool compare_at_site_resolution = true; /**< false: upscale the site version to the original */
  bool auto_visualize = true;             /**< recompute as soon as a slot changes */
};

/** @brief Builds the run-time settings from the configuration. */
[[nodiscard]] Settings settings_from_config(const Config& cfg);

/**
 * @brief A preview for the slot cards, at most `max_px` wide (keeps VRAM and upload time low).
 * @param img Source image.
 * @param max_px `limits.thumbnail_max_px`.
 */
[[nodiscard]] Image make_thumbnail(const Image& img, int max_px);

/** @brief One loaded file (or one decoded video frame). */
struct MediaSource
{
  std::string path;         /**< local file (or temp file for downloads) */
  std::string url;          /**< where it came from, if fetched */
  std::string display_name;
  bool temp_file = false;   /**< delete on exit */
  uint64_t bytes_size = 0;
  FileKind kind = FileKind::Unknown;
  std::string decoder;
  std::string content_type; /**< from the HTTP response */
  JpegInfo jpeg;
  VideoInfo video;
  Image image;              /**< decoded still or current frame */
  Image thumb;              /**< preview built off-thread, uploaded then dropped */
  double frame_time = 0.0;  /**< timestamp of `image` for videos */
  int native_w = 0;
  int native_h = 0;

  [[nodiscard]] bool is_video() const { return kind == FileKind::Video; }
  [[nodiscard]] bool loaded() const { return image.valid(); }

  /**
   * @brief "JPEG 1080x1350, baseline JPEG, 4:2:0, q~78, 245 KB".
   * @param custom_table_threshold_pct `metrics.custom_table_fit_error_pct`.
   */
  [[nodiscard]] std::string format_line(float custom_table_threshold_pct) const;
};

/** @brief One card in the left panel: the original or one site. */
struct Slot
{
  int id = 0;
  bool is_original = false;
  std::string title;
  MediaSource media;
  std::string url_input;
  std::string site_query;
  int site_index = kNoSiteIndex;
  FuzzySiteComboState combo;
  std::string status;
  bool error = false;
  bool loading = false;
  GpuTexture thumb;

  /** @brief Site name, custom text, or the slot title. */
  [[nodiscard]] std::string site_label(const std::vector<SiteInfo>& sites) const;

  /** @brief True when a site was chosen or a custom name typed. */
  [[nodiscard]] bool has_site_name() const;

  /** @brief Value of site_index when no site is chosen. */
  static constexpr int kNoSiteIndex = -1;
};

/** @brief Original versus one site, at a common size. */
struct Comparison
{
  int slot_id = 0;
  bool ok = false;
  std::string error;
  int w = 0;
  int h = 0;
  Image before;
  Image after;
  bool aspect_mismatch = false;
  float resample_scale = 1.0f; /**< comparison size / original size */
  int align_dx = 0;
  int align_dy = 0;
  bool align_dirty = false;
  PairStats stats;
  HeatMap heat;
  Metric heat_metric = Metric::AbsDiff;
  bool heat_blocked = false;
  bool heat_pending = false;
  bool stats_pending = false;
  GpuTexture tex_before;
  GpuTexture tex_after;
  GpuTexture tex_heat;
  double frame_time_before = 0;
  double frame_time_after = 0;
};

/** @brief Lightroom-style compare layouts. */
enum class Layout
{
  Loupe,          /**< one image; '\' toggles before/after */
  LeftRight,      /**< Before | After */
  LeftRightSplit, /**< one image, vertical divider */
  TopBottom,
  TopBottomSplit,
  Survey, /**< every site tiled */
  COUNT
};

/** @brief Number of layouts. */
constexpr int kLayoutCount = static_cast<int>(Layout::COUNT);

/** @brief Display name of a layout. */
[[nodiscard]] const char* layout_name(Layout l);

/** @brief State of the heat-map controls (initialised from the `heatmap` section). */
struct HeatSettings
{
  Metric metric = Metric::DeltaE2000;
  ColorMap cmap = ColorMap::Inferno;
  ViewMode after_mode = ViewMode::Overlay; /**< what the "after" side shows */
  int overlay_base = 1;                    /**< 0 original, 1 site version */
  float intensity = 0.0f;
  bool proportional = true;
  bool auto_scale = true; /**< max = the configured percentile of the map */
  float manual_max = 0.0f;
  float gamma = 1.0f;
  float threshold = 0.0f;
  bool block_view = false; /**< average over JPEG blocks */
  bool log_histogram = true;
};

/** @brief Builds the heat-map state from the configuration. */
[[nodiscard]] HeatSettings heat_settings_from_config(const HeatMapConfig& cfg);

/** @brief Zoom, pan and split state of the compare view. */
struct ViewState
{
  float zoom = 0.0f; /**< 0 = fit, otherwise screen px per image px */
  float center_x = 0.5f; /**< image-normalised point at the view centre */
  float center_y = 0.5f;
  float split = 0.5f; /**< divider position 0..1 within the pane */
  bool split_invert = false;
  bool link_focus = true;         /**< synchronised zoom/pan (Lightroom's Link Focus) */
  bool loupe_show_before = false; /**< '\' state in Loupe layout */
  bool dragging_split = false;
  bool panning = false;

  /** @brief Back to "fit". */
  void reset_zoom()
  {
    zoom = 0.0f;
    center_x = 0.5f;
    center_y = 0.5f;
  }
};

/** @brief The application. */
class App
{
public:
  /**
   * @param window The GLFW window (context already current, gl::load() done).
   * @param config The loaded configuration (copied).
   * @param dpi_scale Monitor content scale.
   */
  App(GLFWwindow* window, const Config& config, float dpi_scale);
  ~App();
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  /**
   * @brief Creates the renderer and locates ffmpeg.
   * @param err Receives the renderer error when it fails.
   * @return true when the renderer works.
   */
  [[nodiscard]] bool init(std::string& err);

  /** @brief Builds one frame of UI (call between ImGui::NewFrame and ImGui::Render). */
  void frame();

  /** @brief GLFW drop callback: files go to the hovered slot, then to empty slots. */
  void on_drop(int count, const char** paths);

  /** @brief True while something animates or works, so the main loop should not sleep. */
  [[nodiscard]] bool wants_fast_frames() const;

  /** @brief The configuration this App runs with. */
  [[nodiscard]] const Config& config() const { return config_; }

private:
  /* ---- UI: ui_slots.cpp ------------------------------------------------- */
  void ui_slots_panel();
  void ui_slot(Slot& slot);
  void ui_slot_header(Slot& slot, float avail);
  void ui_slot_dropzone(Slot& slot, float avail, float thumb_h);
  void ui_slot_hover_details(const Slot& slot);
  void ui_slot_context_menu(Slot& slot);
  void ui_slot_url_line(Slot& slot, float avail);
  void ui_visualize_button();

  /* ---- UI: ui_compare.cpp ----------------------------------------------- */
  void ui_viewer();
  void ui_viewer_toolbar();
  void ui_viewer_site_selector();
  void ui_viewer_zoom_controls();
  void handle_shortcuts();
  void handle_view_keys(bool shift, bool alt);
  void handle_zoom_keys(bool ctrl);
  void handle_heat_keys(bool shift);
  void handle_file_keys(bool ctrl);

  /* ---- UI: ui_panels.cpp ------------------------------------------------ */
  void ui_menubar();
  void ui_file_menu();
  void ui_view_menu();
  void ui_right_panel();
  void ui_heat_panel();
  void ui_heat_metric_and_mode();
  void ui_heat_overlay_controls();
  void ui_colormap_combo();
  void ui_heat_scale_controls();
  void ui_heat_legend();
  void ui_stats_panel(Comparison* comp);
  void ui_stats_file_rows(const Slot& site);
  void ui_stats_still_rows(const Slot& site);
  void ui_stats_video_rows(const Slot& site, const Comparison& comp);
  void ui_stats_quality_rows(const PairStats& s);
  void ui_error_histogram(const Comparison& comp);
  void ui_alignment_controls(Comparison& comp);
  void ui_video_panel();
  void ui_video_timeline(const MediaSource& ref);
  void ui_video_sampling(const MediaSource& ref);
  void ui_video_sample_plots(double duration);
  void start_video_sampling();
  void ui_settings_window();
  void ui_settings_acceleration();
  void ui_settings_video();
  void ui_settings_display();
  void ui_about_window();
  void ui_status_bar();

  /* ---- actions: app.cpp ------------------------------------------------- */
  void load_slot_path(Slot& slot, const std::string& path);
  void load_slot_url(Slot& slot, const std::string& url);
  void detect_site(Slot& slot, const std::string& url);
  void layout_main_window();
  void load_from_clipboard(Slot* preferred);
  void apply_media(Slot& slot, MediaSource media, const std::string& error);
  void seek_all(double t);
  void seek_slot(Slot& slot, double t, bool then_visualize);
  void clear_slot(Slot& slot);
  void add_site_slot();
  void remove_site_slot(int index);
  void visualize_all();
  void visualize(int site_index);
  void request_heat(Comparison& comp);
  void invalidate_comparisons();
  void release_comparison(Comparison& comp);
  [[nodiscard]] Comparison* comparison_for(int slot_id);
  [[nodiscard]] Comparison& comparison_for_slot(int slot_id);
  [[nodiscard]] Slot* slot_by_id(int id);
  [[nodiscard]] Slot* next_empty_slot(const Slot* exclude);
  [[nodiscard]] Slot* active_site();
  [[nodiscard]] Comparison* active_comparison();
  void export_heat_png();
  void cycle_layout(int dir);
  void set_layout(Layout l);
  void zoom_step(int dir, float anchor_x, float anchor_y);
  void toggle_zoom_1_1();
  void toast(const std::string& message, double seconds);
  void touch();
  [[nodiscard]] std::string temp_path(const std::string& ext);
  [[nodiscard]] float ui_scale() const { return dpi_scale_ * settings_.ui_scale; }
  [[nodiscard]] float heat_scale_max(const Comparison* comp) const;
  [[nodiscard]] float clamped_gamma() const;

  Config config_;
  GLFWwindow* window_ = nullptr;
  float dpi_scale_ = 1.0f;
  Renderer renderer_;
  BackgroundWorker worker_;
  FfmpegTools ffmpeg_;
  DialogTools dialogs_;
  Settings settings_;

  Slot original_;
  std::vector<Slot> sites_;
  std::vector<std::unique_ptr<Comparison>> comps_;
  int next_slot_id_ = 1;
  int active_site_ = 0;
  Layout layout_ = Layout::Loupe;
  HeatSettings heat_;
  ViewState view_;
  double video_time_ = 0.0;
  float video_scrub_ = 0.0f; /**< timeline slider value while dragging */
  float fit_scale_ = 1.0f;   /**< screen px per image px at "fit", updated by the viewer every frame */
  bool show_left_panel_ = true;
  bool show_right_panel_ = true;
  bool show_settings_ = false;
  bool show_about_ = false;
  std::string toast_;
  double toast_until_ = 0.0;
  int hovered_drop_slot_ = -1; /**< slot id under the mouse (drag-and-drop target) */
  FrameDraws draws_;
  bool hover_valid_ = false;
  int hover_px_ = 0; /**< hovered pixel in comparison space */
  int hover_py_ = 0;
  std::vector<std::string> temp_files_;
  double last_interaction_ = 0.0;
  bool pending_visualize_ = false;
  bool renderer_ok_ = false;
  std::string renderer_error_;
  std::vector<float> sample_psnr_; /**< video sampling results */
  std::vector<float> sample_ssim_;
  int sample_count_ = 1;
  bool sampling_ = false;

  friend struct ViewerFrame;
};

} // namespace cc
