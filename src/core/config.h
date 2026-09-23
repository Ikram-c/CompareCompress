/**
 * @file config.h
 * @brief Typed view of config.yaml: every tunable number the application uses.
 *
 * The defaults are compiled in from config/config.yaml; an external file with
 * the same layout overrides any subset of keys (see load_config()).  Nothing
 * outside this module reads YAML: the rest of the code receives the typed
 * structs below, so a typo in a key is reported once, at start-up, with the
 * key path and the file that contained it.
 */
#pragma once

#include "core/accel.h"
#include "core/colormap.h"
#include "core/metric.h"
#include "core/view_mode.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cc {

/** @brief Initial window size at 100 % DPI scale. */
struct WindowConfig
{
  int width_px = 0;
  int height_px = 0;
};

/** @brief Frame pacing of the main loop. */
struct FrameTimingConfig
{
  double fast_interval_s = 0;         /**< frame period while interacting or working */
  double idle_interval_s = 0;         /**< frame period when idle */
  double idle_after_interaction_s = 0; /**< stay fast this long after the last input */
  int iconified_sleep_ms = 0;         /**< sleep per loop iteration while minimised */
};

/** @brief Tooltip hover delays handed to Dear ImGui. */
struct TooltipConfig
{
  float hover_delay_short_s = 0;
  float hover_delay_normal_s = 0;
  float hover_stationary_delay_s = 0;
};

/** @brief How long status-bar messages stay visible. */
struct ToastConfig
{
  double short_s = 0;
  double normal_s = 0;
  double long_s = 0;
};

/** @brief Panel and window geometry at 100 % scale. */
struct PanelConfig
{
  float left_width_px = 0;
  float left_max_fraction = 0;
  float right_width_px = 0;
  float right_max_fraction = 0;
  float slot_thumb_height_fraction = 0;
  float slot_thumb_min_px = 0;
  float slot_thumb_max_px = 0;
  float settings_window_width_px = 0;
  float about_window_width_px = 0;
  float plot_height_px = 0;
  float histogram_height_px = 0;
};

/** @brief The `app` section. */
struct AppConfig
{
  std::string name;
  WindowConfig window;
  float ui_scale = 0;
  bool power_saver = false;
  bool auto_visualize = false;
  bool compare_at_site_resolution = false;
  bool show_loupe = false;
  bool show_fps = false;
  FrameTimingConfig frame_timing;
  TooltipConfig tooltip;
  ToastConfig toast;
  PanelConfig panels;
};

/** @brief Caps applied to videos before and after ffprobe runs. */
struct VideoLimits
{
  uint64_t max_bytes = 0;
  double max_duration_s = 0;
  int max_edge_px = 0; /**< longer edge, either orientation */
};

/** @brief The `limits` section: everything that bounds memory, time and loops. */
struct LimitsConfig
{
  uint64_t max_image_bytes = 0;
  uint64_t max_download_bytes = 0;
  int max_image_edge_px = 0;
  int max_temp_files = 0;
  VideoLimits video;
  std::size_t sniff_head_bytes = 0;
  int thumbnail_max_px = 0;
  int max_site_slots = 0;
  int max_query_chars = 0;
  int max_site_name_chars = 0;
  int max_dropped_files = 0;
  int max_url_chars = 0;
};

/** @brief SSIM window and stabilising constants. */
struct SsimConfig
{
  int window = 0;
  float sigma = 0;
  float k1 = 0;
  float k2 = 0;
};

/** @brief The `metrics` section. */
struct MetricsConfig
{
  SsimConfig ssim;
  float jnd_delta_e2000 = 0;
  int histogram_bins = 0;
  int histogram_fine_bins = 0;
  int auto_scale_percentile = 0;
  int block_size_px = 0;
  double psnr_identical_db = 0;
  float aspect_mismatch_tolerance = 0;
  float custom_table_fit_error_pct = 0;
  float default_scale[kMetricCount] = {};
};

/** @brief The `heatmap` section: initial state of the heat-map controls. */
struct HeatMapConfig
{
  Metric metric = Metric::AbsDiff;
  ColorMap colormap = ColorMap::Gray;
  ViewMode after_mode = ViewMode::After;
  int overlay_base = 0; /**< 0 = original, 1 = site copy */
  float intensity = 0;
  float intensity_step = 0;
  bool proportional = false;
  bool auto_scale = false;
  float manual_max = 0;
  float manual_max_min_factor = 0;
  float manual_max_max_factor = 0;
  float gamma = 0;
  float gamma_min = 0;
  float gamma_max = 0;
  float gamma_floor = 0;
  float threshold = 0;
  bool block_view = false;
  bool log_histogram = false;
  int legend_swatches = 0;
  float legend_height_px = 0;
  int colormap_preview_steps = 0;
  float colormap_preview_width_px = 0;
};

/** @brief Zoom stepping and limits. */
struct ZoomConfig
{
  float step_below_2x = 0;
  float step_above_2x = 0;
  float max = 0;
  float min_fit_fraction = 0;
  float snap_epsilon = 0;
};

/** @brief Corner loupe geometry. */
struct LoupeConfig
{
  int zoom = 0;
  int min_zoom = 0;
  int max_zoom = 0;
  float size_fraction = 0;
  float min_size_px = 0;
  float max_size_px = 0;
  float margin_px = 0;
  float cursor_avoid_px = 0;
};

/** @brief Split-divider hit testing and handle size. */
struct SplitConfig
{
  float line_hit_px = 0;
  float handle_hit_px = 0;
  float handle_radius_fraction = 0;
  float handle_radius_max_px = 0;
};

/** @brief The `view` section. */
struct ViewConfig
{
  ZoomConfig zoom;
  LoupeConfig loupe;
  SplitConfig split;
  float pane_gap_px = 0;
  float pan_drag_threshold_px = 0;
  float canvas_min_px = 0;
  int preview_alpha = 0;
  float progress_bar_width_px = 0;
};

/** @brief The `fuzzy` section: site-search scoring. */
struct FuzzyConfig
{
  int base_score = 0;
  int sequential_bonus = 0;
  int separator_bonus = 0;
  int camel_bonus = 0;
  int first_letter_bonus = 0;
  int leading_letter_penalty = 0;
  int max_leading_letter_penalty = 0;
  int unmatched_letter_penalty = 0;
  int exact_substring_bonus = 0;
  int dropdown_rows = 0;
  float dropdown_min_width_px = 0;
};

/** @brief WinHTTP time-outs (Windows builds without libcurl). */
struct WinHttpConfig
{
  int resolve_timeout_ms = 0;
  int connect_timeout_ms = 0;
  int send_timeout_ms = 0;
  int receive_timeout_ms = 0;
};

/** @brief The `network` section. */
struct NetworkConfig
{
  std::string user_agent;
  std::string accept;
  long connect_timeout_s = 0;
  long low_speed_limit_bytes_per_s = 0;
  long low_speed_time_s = 0;
  long max_redirects = 0;
  std::size_t read_chunk_bytes = 0;
  WinHttpConfig winhttp;
};

/** @brief The `process` section: running ffmpeg, ffprobe and dialog helpers. */
struct ProcessConfig
{
  int default_timeout_ms = 0;
  int probe_timeout_ms = 0;
  int version_timeout_ms = 0;
  int dialog_timeout_ms = 0;
  uint64_t stderr_cap_bytes = 0;
  uint64_t probe_stdout_cap_bytes = 0;
  uint64_t version_stdout_cap_bytes = 0;
  uint64_t dialog_stdout_cap_bytes = 0;
  std::size_t pipe_chunk_bytes = 0;
  uint64_t frame_cap_slack_bytes = 0;
  long max_read_iterations = 0;
};

/** @brief The `ffmpeg` section. */
struct FfmpegConfig
{
  std::vector<std::string> search_subdirs;
  std::vector<std::string> extra_search_dirs;
  std::string scale_filter;
  double seek_end_margin_s = 0;
  double fallback_frame_step_s = 0;
  int max_still_edge_px = 0;
  int sample_points = 0;
  int sample_points_min = 0;
  int sample_points_max = 0;
};

/** @brief The `threading` section. */
struct ThreadingConfig
{
  int max_workers = 0;
  int fallback_workers = 0;
  int min_rows_per_worker = 0;
};

/** @brief One entry of the site database. */
struct SiteInfo
{
  std::string name;                  /**< display name */
  std::vector<std::string> aliases;  /**< lower-case search aliases */
  std::vector<std::string> hosts;    /**< host suffixes for URL auto-detection */
  std::string notes;                 /**< what the site typically does to uploads */
};

/** @brief The whole configuration. */
struct Config
{
  int schema_version = 0;
  AppConfig app;
  LimitsConfig limits;
  MetricsConfig metrics;
  HeatMapConfig heatmap;
  ViewConfig view;
  FuzzyConfig fuzzy;
  NetworkConfig network;
  ProcessConfig process;
  FfmpegConfig ffmpeg;
  ThreadingConfig threading;
  AccelerationConfig acceleration;
  std::vector<SiteInfo> sites;
};

/** @brief The schema version this build understands. */
constexpr int kConfigSchemaVersion = 1;

/** @brief The built-in config.yaml text compiled into the executable. */
const char* builtin_config_yaml();

/**
 * @brief Parses YAML text into a Config.
 * @param yaml_text The document (built-in defaults already merged in when
 *        called through load_config()).
 * @param source Label used in error messages (a file path or "built-in").
 * @param out Receives the parsed configuration.
 * @param err Receives a message naming the offending key on failure.
 * @return true on success.
 */
bool parse_config(const std::string& yaml_text, const std::string& source, Config& out, std::string& err);

/**
 * @brief Loads the built-in defaults, then merges an optional override file.
 * @param override_path Path of a config.yaml to merge, or "" for none.
 * @param out Receives the configuration.
 * @param err Receives a message on failure.
 * @return true on success.
 */
bool load_config(const std::string& override_path, Config& out, std::string& err);

/** @brief The built-in configuration; aborts the process if it does not parse. */
const Config& default_config();

/**
 * @brief Per-user override file: `~/Library/Application Support/CompressCompare/config.yaml`
 * on macOS, `$XDG_CONFIG_HOME/compresscompare/config.yaml` (default `~/.config/...`) on Linux,
 * "" on Windows or when HOME is not set.  The file does not have to exist.
 */
std::string user_config_path();

/**
 * @brief Finds the override file to load: `explicit_path` if given, else
 * `config.yaml` next to the executable, else user_config_path(), else
 * `config.yaml` in the working directory.
 * @param explicit_path Path from the command line, or "".
 * @param exe_dir Directory of the running executable, or "".
 * @return The path to merge, or "" when no override exists.
 */
std::string find_config_override(const std::string& explicit_path, const std::string& exe_dir);

} // namespace cc
