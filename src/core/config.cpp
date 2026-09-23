/**
 * @file config.cpp
 * @brief Reads config.yaml with yaml-cpp: built-in defaults, override merging
 * and validation of every key.
 *
 * Only this file includes yaml-cpp.  All calls into the library happen inside
 * one try block per entry point so that the rest of the program never sees an
 * exception: every problem is turned into `false` plus a message that names
 * the file and the key path.
 */
#include "core/config.h"

#include "util/contract.h"

#include "cc_builtin_config.h"

#include <yaml-cpp/yaml.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace cc {

namespace {

/* ---- formal loop bounds ------------------------------------------------ */

/** @brief An override file larger than this is refused. */
constexpr std::size_t kMaxConfigFileBytes = static_cast<std::size_t>(4) * 1024 * 1024;
/** @brief Sites accepted after merging. */
constexpr std::size_t kMaxSites = 512;
/** @brief Aliases or hosts accepted per site. */
constexpr std::size_t kMaxSiteStrings = 64;
/** @brief Search directories accepted in the ffmpeg section. */
constexpr std::size_t kMaxSearchDirs = 32;
/** @brief Map entries visited while merging or checking one document. */
constexpr std::size_t kMaxMapEntries = 8192;
/** @brief Nesting depth of maps that is walked. */
constexpr int kMaxDepth = 8;
/** @brief Components of a dotted key path. */
constexpr std::size_t kMaxPathParts = 8;

/* ---- validation bounds ------------------------------------------------- */

constexpr int kMinWindowPx = 200;
constexpr int kMaxWindowPx = 32768;
constexpr float kMinUiScale = 0.25f;
constexpr float kMaxUiScale = 8.0f;
constexpr double kMaxSeconds = 3600.0;
constexpr int kMaxMilliseconds = 3600000;
constexpr float kMaxPixels = 65536.0f;
constexpr int kMaxEdgePx = 65536;
constexpr uint64_t kMaxBytes = static_cast<uint64_t>(16) << 30; /**< 16 GiB */
constexpr int kMaxCount = 16777216;
constexpr int kMaxScore = 10000;
constexpr int kMaxWorkers = 1024;
constexpr float kMinSelfTestTolerance = 1e-6f;
constexpr float kMaxZoom = 256.0f;
constexpr int kMaxAlpha = 255;
constexpr int kMaxPercentile = 100;
constexpr float kMaxScale = 1.0e6f;
constexpr float kTinyPositive = 1.0e-6f;   /**< smallest allowed strictly positive value */
constexpr float kMinRatio = 0.01f;         /**< smallest allowed fraction / step */
constexpr float kMinFineRatio = 0.001f;    /**< smallest allowed fine step */
constexpr float kMinPanelFraction = 0.05f; /**< a panel cannot be narrower than this share of the window */
constexpr float kMinZoomStep = 1.001f;     /**< a zoom step must enlarge */
constexpr float kMaxZoomStep = 10.0f;
constexpr float kMinSigma = 0.1f;
constexpr double kMaxPsnrDb = 1000.0;
constexpr double kHoursPerDay = 24.0;
constexpr int kMaxDropdownRows = 1000;
constexpr double kMinFrameStepS = 1.0e-6;

/** @brief Site fields an entry may contain. */
const char* const kSiteKeys[] = {"name", "aliases", "hosts", "notes"};
constexpr std::size_t kSiteKeyCount = sizeof(kSiteKeys) / sizeof(kSiteKeys[0]);

/**
 * @brief Looks a key up in a map without inserting it.
 * @param parent A map node (checked by the caller).
 * @param key The key.
 * @return The child, undefined when absent.
 */
YAML::Node child_of(const YAML::Node& parent, const std::string& key) { return parent[key]; }

/**
 * @brief Splits `a.b.c` into its components.
 * @param path Dotted key path.
 * @return The components; at most kMaxPathParts.
 */
std::vector<std::string> split_path(const std::string& path)
{
  std::vector<std::string> parts;
  std::string current;
  for(std::size_t i = 0; i < path.size() && parts.size() < kMaxPathParts; ++i)
  {
    if(path[i] == '.')
    {
      parts.push_back(current);
      current.clear();
    }
    else
    {
      current.push_back(path[i]);
    }
  }
  if(parts.size() < kMaxPathParts)
  {
    parts.push_back(current);
  }
  return parts;
}

/**
 * @brief Typed, validating access to one YAML document.
 *
 * Every getter records the key path it consumed so that check_unknown_keys()
 * can flag misspelt keys, and stores the first error in error().
 */
class Reader
{
public:
  Reader(const YAML::Node& root, std::string source) : root_(root), source_(std::move(source)) {}

  /** @brief The first error recorded, or "" when everything succeeded. */
  const std::string& error() const { return err_; }

  /**
   * @brief Records an error for a key path.
   * @return false, so callers can `return fail(...)`.
   */
  bool fail(const std::string& path, const std::string& what)
  {
    if(err_.empty())
    {
      err_ = source_ + ": " + (path.empty() ? std::string("document") : path) + ": " + what;
    }
    return false;
  }

  /**
   * @brief Finds the node at a dotted path and marks the path as consumed.
   * @param path Dotted key path.
   * @param out Receives the node.
   * @return false (with an error) when a component is missing or not a map.
   */
  bool find(const std::string& path, YAML::Node& out)
  {
    const std::vector<std::string> parts = split_path(path);
    YAML::Node node = root_;
    std::string walked;
    for(std::size_t i = 0; i < parts.size() && i < kMaxPathParts; ++i)
    {
      if(!node.IsDefined() || !node.IsMap())
      {
        return fail(walked, "expected a map");
      }
      const YAML::Node child = child_of(node, parts[i]);
      if(!walked.empty())
      {
        walked += '.';
      }
      walked += parts[i];
      if(!child.IsDefined())
      {
        return fail(walked, "missing key");
      }
      /* reset() rebinds the handle; operator= would rewrite the document. */
      node.reset(child);
    }
    used_.insert(path);
    out.reset(node);
    return true;
  }

  /**
   * @brief Reads a number and checks that it lies in [lo, hi].
   * @tparam T An arithmetic type yaml-cpp can decode.
   */
  template <typename T>
  bool get_number(const std::string& path, T& out, T lo, T hi)
  {
    CC_REQUIRE(lo <= hi, return fail(path, "internal: empty range"));
    YAML::Node node;
    if(!find(path, node))
    {
      return false;
    }
    T value{};
    if(!node.IsScalar() || !YAML::convert<T>::decode(node, value))
    {
      return fail(path, "expected a number, got '" + node.Scalar() + "'");
    }
    if(value < lo || value > hi)
    {
      return fail(path, "value " + node.Scalar() + " is outside [" + std::to_string(lo) + ", " + std::to_string(hi) + "]");
    }
    out = value;
    return true;
  }

  bool get_int(const std::string& path, int& out, int lo, int hi) { return get_number<int>(path, out, lo, hi); }
  bool get_long(const std::string& path, long& out, long lo, long hi) { return get_number<long>(path, out, lo, hi); }
  bool get_float(const std::string& path, float& out, float lo, float hi) { return get_number<float>(path, out, lo, hi); }
  bool get_double(const std::string& path, double& out, double lo, double hi) { return get_number<double>(path, out, lo, hi); }

  bool get_u64(const std::string& path, uint64_t& out, uint64_t lo, uint64_t hi)
  {
    unsigned long long value = 0;
    if(!get_number<unsigned long long>(path, value, static_cast<unsigned long long>(lo), static_cast<unsigned long long>(hi)))
    {
      return false;
    }
    out = static_cast<uint64_t>(value);
    return true;
  }

  bool get_size(const std::string& path, std::size_t& out, std::size_t lo, std::size_t hi)
  {
    uint64_t value = 0;
    if(!get_u64(path, value, static_cast<uint64_t>(lo), static_cast<uint64_t>(hi)))
    {
      return false;
    }
    out = static_cast<std::size_t>(value);
    return true;
  }

  bool get_bool(const std::string& path, bool& out)
  {
    YAML::Node node;
    if(!find(path, node))
    {
      return false;
    }
    bool value = false;
    if(!node.IsScalar() || !YAML::convert<bool>::decode(node, value))
    {
      return fail(path, "expected true or false, got '" + node.Scalar() + "'");
    }
    out = value;
    return true;
  }

  bool get_string(const std::string& path, std::string& out, bool allow_empty)
  {
    YAML::Node node;
    if(!find(path, node))
    {
      return false;
    }
    if(!node.IsScalar())
    {
      return fail(path, "expected a string");
    }
    if(!allow_empty && node.Scalar().empty())
    {
      return fail(path, "must not be empty");
    }
    out = node.Scalar();
    return true;
  }

  /**
   * @brief Reads a sequence of non-empty strings.
   * @param path Dotted key path.
   * @param out Receives the items.
   * @param max_items Upper bound on the number of items.
   * @return false (with the error recorded) on a missing key, a non-list or too many items.
   */
  bool get_string_list(const std::string& path, std::vector<std::string>& out, std::size_t max_items)
  {
    YAML::Node node;
    if(!find(path, node))
    {
      return false;
    }
    return read_string_list(node, path, out, max_items);
  }

  /**
   * @brief Reads a sequence node of non-empty strings (shared with site entries).
   */
  bool read_string_list(const YAML::Node& node, const std::string& path, std::vector<std::string>& out, std::size_t max_items)
  {
    if(!node.IsDefined() || !node.IsSequence())
    {
      return fail(path, "expected a list");
    }
    const std::size_t count = node.size();
    if(count > max_items)
    {
      return fail(path, "more than " + std::to_string(max_items) + " entries");
    }
    std::vector<std::string> items;
    items.reserve(count);
    for(std::size_t i = 0; i < count && i < max_items; ++i)
    {
      const YAML::Node item = node[i];
      if(!item.IsScalar() || item.Scalar().empty())
      {
        return fail(path + "[" + std::to_string(i) + "]", "expected a non-empty string");
      }
      items.push_back(item.Scalar());
    }
    out = std::move(items);
    return true;
  }

  /**
   * @brief Reads a scalar and converts it with a key parser such as metric_from_key().
   * @tparam E The enum type.
   * @param path Dotted key path.
   * @param out Receives the value.
   * @param parse Converts a key to the enum; returns false for unknown keys.
   * @param choices Comma-separated list of accepted keys, quoted in the error message.
   * @return false (with the error recorded) when the key is missing or not one of the choices.
   */
  template <typename E>
  bool get_enum(const std::string& path, E& out, bool (*parse)(const std::string&, E&), const char* choices)
  {
    std::string key;
    if(!get_string(path, key, false))
    {
      return false;
    }
    E value{};
    if(!parse(key, value))
    {
      return fail(path, "'" + key + "' is not one of " + choices);
    }
    out = value;
    return true;
  }

  /**
   * @brief Marks a whole subtree as consumed (its entries are validated elsewhere).
   */
  void consume_subtree(const std::string& path) { whole_.insert(path); }

  /**
   * @brief Walks every map entry and reports the first leaf no getter consumed.
   * @return false (with an error naming the key) when an unknown key exists.
   */
  bool check_unknown_keys()
  {
    struct Item
    {
      YAML::Node node;
      std::string path;
      int depth;
    };
    std::vector<Item> work;
    work.push_back(Item{root_, std::string(), 0});
    std::size_t visited = 0;
    while(!work.empty() && visited < kMaxMapEntries)
    {
      const Item item = work.back();
      work.pop_back();
      if(item.depth > kMaxDepth)
      {
        return fail(item.path, "nested too deeply");
      }
      for(YAML::const_iterator it = item.node.begin(); it != item.node.end() && visited < kMaxMapEntries; ++it)
      {
        ++visited;
        if(!it->first.IsScalar())
        {
          return fail(item.path, "keys must be strings");
        }
        const std::string path = item.path.empty() ? it->first.Scalar() : item.path + "." + it->first.Scalar();
        if(whole_.count(path) != 0)
        {
          continue;
        }
        if(it->second.IsDefined() && it->second.IsMap())
        {
          work.push_back(Item{it->second, path, item.depth + 1});
        }
        else if(used_.count(path) == 0)
        {
          return fail(path, "unknown key (check the spelling against docs/reference/config.md)");
        }
      }
    }
    return work.empty() ? true : fail(std::string(), "too many entries");
  }

private:
  YAML::Node root_;
  std::string source_;
  std::string err_;
  std::set<std::string> used_;
  std::set<std::string> whole_;
};

/* ---- section readers --------------------------------------------------- */

bool read_window(Reader& r, WindowConfig& c)
{
  return r.get_int("app.window.width_px", c.width_px, kMinWindowPx, kMaxWindowPx)
      && r.get_int("app.window.height_px", c.height_px, kMinWindowPx, kMaxWindowPx);
}

bool read_frame_timing(Reader& r, FrameTimingConfig& c)
{
  return r.get_double("app.frame_timing.fast_interval_s", c.fast_interval_s, 0.0, kMaxSeconds)
      && r.get_double("app.frame_timing.idle_interval_s", c.idle_interval_s, 0.0, kMaxSeconds)
      && r.get_double("app.frame_timing.idle_after_interaction_s", c.idle_after_interaction_s, 0.0, kMaxSeconds)
      && r.get_int("app.frame_timing.iconified_sleep_ms", c.iconified_sleep_ms, 0, kMaxMilliseconds);
}

bool read_tooltip(Reader& r, TooltipConfig& c)
{
  const float max_delay = static_cast<float>(kMaxSeconds);
  return r.get_float("app.tooltip.hover_delay_short_s", c.hover_delay_short_s, 0.0f, max_delay)
      && r.get_float("app.tooltip.hover_delay_normal_s", c.hover_delay_normal_s, 0.0f, max_delay)
      && r.get_float("app.tooltip.hover_stationary_delay_s", c.hover_stationary_delay_s, 0.0f, max_delay);
}

bool read_toast(Reader& r, ToastConfig& c)
{
  return r.get_double("app.toast.short_s", c.short_s, 0.0, kMaxSeconds)
      && r.get_double("app.toast.normal_s", c.normal_s, 0.0, kMaxSeconds)
      && r.get_double("app.toast.long_s", c.long_s, 0.0, kMaxSeconds);
}

bool read_panels(Reader& r, PanelConfig& c)
{
  return r.get_float("app.panels.left_width_px", c.left_width_px, 1.0f, kMaxPixels)
      && r.get_float("app.panels.left_max_fraction", c.left_max_fraction, kMinPanelFraction, 1.0f)
      && r.get_float("app.panels.right_width_px", c.right_width_px, 1.0f, kMaxPixels)
      && r.get_float("app.panels.right_max_fraction", c.right_max_fraction, kMinPanelFraction, 1.0f)
      && r.get_float("app.panels.slot_thumb_height_fraction", c.slot_thumb_height_fraction, kMinPanelFraction, 2.0f)
      && r.get_float("app.panels.slot_thumb_min_px", c.slot_thumb_min_px, 1.0f, kMaxPixels)
      && r.get_float("app.panels.slot_thumb_max_px", c.slot_thumb_max_px, 1.0f, kMaxPixels)
      && r.get_float("app.panels.settings_window_width_px", c.settings_window_width_px, 1.0f, kMaxPixels)
      && r.get_float("app.panels.about_window_width_px", c.about_window_width_px, 1.0f, kMaxPixels)
      && r.get_float("app.panels.plot_height_px", c.plot_height_px, 1.0f, kMaxPixels)
      && r.get_float("app.panels.histogram_height_px", c.histogram_height_px, 1.0f, kMaxPixels);
}

bool read_app(Reader& r, AppConfig& c)
{
  return r.get_string("app.name", c.name, false)
      && read_window(r, c.window)
      && r.get_float("app.ui_scale", c.ui_scale, kMinUiScale, kMaxUiScale)
      && r.get_bool("app.power_saver", c.power_saver)
      && r.get_bool("app.auto_visualize", c.auto_visualize)
      && r.get_bool("app.compare_at_site_resolution", c.compare_at_site_resolution)
      && r.get_bool("app.show_loupe", c.show_loupe)
      && r.get_bool("app.show_fps", c.show_fps)
      && read_frame_timing(r, c.frame_timing)
      && read_tooltip(r, c.tooltip)
      && read_toast(r, c.toast)
      && read_panels(r, c.panels);
}

bool read_limits(Reader& r, LimitsConfig& c)
{
  return r.get_u64("limits.max_image_bytes", c.max_image_bytes, 1, kMaxBytes)
      && r.get_u64("limits.max_download_bytes", c.max_download_bytes, 1, kMaxBytes)
      && r.get_int("limits.max_image_edge_px", c.max_image_edge_px, 1, kMaxEdgePx)
      && r.get_int("limits.max_temp_files", c.max_temp_files, 1, kMaxCount)
      && r.get_u64("limits.video.max_bytes", c.video.max_bytes, 1, kMaxBytes)
      && r.get_double("limits.video.max_duration_s", c.video.max_duration_s, 0.0, kMaxSeconds * kHoursPerDay)
      && r.get_int("limits.video.max_edge_px", c.video.max_edge_px, 1, kMaxEdgePx)
      && r.get_size("limits.sniff_head_bytes", c.sniff_head_bytes, 16, static_cast<std::size_t>(kMaxCount))
      && r.get_int("limits.thumbnail_max_px", c.thumbnail_max_px, 1, kMaxEdgePx)
      && r.get_int("limits.max_site_slots", c.max_site_slots, 1, kMaxCount)
      && r.get_int("limits.max_query_chars", c.max_query_chars, 1, kMaxCount)
      && r.get_int("limits.max_site_name_chars", c.max_site_name_chars, 1, kMaxCount)
      && r.get_int("limits.max_dropped_files", c.max_dropped_files, 1, kMaxCount)
      && r.get_int("limits.max_url_chars", c.max_url_chars, 1, kMaxCount);
}

bool read_default_scale(Reader& r, MetricsConfig& c)
{
  bool ok = true;
  for(int i = 0; i < kMetricCount && ok; ++i)
  {
    const std::string path = std::string("metrics.default_scale.") + metric_key(static_cast<Metric>(i));
    ok = r.get_float(path, c.default_scale[i], kTinyPositive, kMaxScale);
  }
  return ok;
}

bool read_metrics(Reader& r, MetricsConfig& c)
{
  return r.get_int("metrics.ssim.window", c.ssim.window, 3, 255)
      && r.get_float("metrics.ssim.sigma", c.ssim.sigma, kMinSigma, 100.0f)
      && r.get_float("metrics.ssim.k1", c.ssim.k1, kTinyPositive, 1.0f)
      && r.get_float("metrics.ssim.k2", c.ssim.k2, kTinyPositive, 1.0f)
      && r.get_float("metrics.jnd_delta_e2000", c.jnd_delta_e2000, 0.0f, 100.0f)
      && r.get_int("metrics.histogram_bins", c.histogram_bins, 2, kMaxCount)
      && r.get_int("metrics.histogram_fine_bins", c.histogram_fine_bins, 2, kMaxCount)
      && r.get_int("metrics.auto_scale_percentile", c.auto_scale_percentile, 1, kMaxPercentile)
      && r.get_int("metrics.block_size_px", c.block_size_px, 1, 1024)
      && r.get_double("metrics.psnr_identical_db", c.psnr_identical_db, 0.0, kMaxPsnrDb)
      && r.get_float("metrics.aspect_mismatch_tolerance", c.aspect_mismatch_tolerance, 0.0f, 1.0f)
      && r.get_float("metrics.custom_table_fit_error_pct", c.custom_table_fit_error_pct, 0.0f, 100.0f)
      && read_default_scale(r, c);
}

/**
 * @brief Parses the `heatmap.overlay_base` key (`original` or `site`).
 */
bool overlay_base_from_key(const std::string& key, int& out)
{
  if(key == "original")
  {
    out = 0;
    return true;
  }
  if(key == "site")
  {
    out = 1;
    return true;
  }
  return false;
}

bool read_heatmap_state(Reader& r, HeatMapConfig& c)
{
  return r.get_enum<Metric>("heatmap.metric", c.metric, &metric_from_key,
                            "abs_diff, luma_diff, delta_e76, delta_e2000, ssim, squared_error")
      && r.get_enum<ColorMap>("heatmap.colormap", c.colormap, &colormap_from_key, "inferno, viridis, heat, ice, alert, gray")
      && r.get_enum<ViewMode>("heatmap.after_mode", c.after_mode, &view_mode_from_key, "after, heat, overlay")
      && r.get_enum<int>("heatmap.overlay_base", c.overlay_base, &overlay_base_from_key, "original, site")
      && r.get_float("heatmap.intensity", c.intensity, 0.0f, 1.0f)
      && r.get_float("heatmap.intensity_step", c.intensity_step, kMinFineRatio, 1.0f)
      && r.get_bool("heatmap.proportional", c.proportional)
      && r.get_bool("heatmap.auto_scale", c.auto_scale)
      && r.get_float("heatmap.manual_max", c.manual_max, kTinyPositive, kMaxScale)
      && r.get_float("heatmap.manual_max_min_factor", c.manual_max_min_factor, kTinyPositive, kMaxScale)
      && r.get_float("heatmap.manual_max_max_factor", c.manual_max_max_factor, kTinyPositive, kMaxScale)
      && r.get_float("heatmap.gamma", c.gamma, kMinRatio, 100.0f)
      && r.get_float("heatmap.gamma_min", c.gamma_min, kMinRatio, 100.0f)
      && r.get_float("heatmap.gamma_max", c.gamma_max, kMinRatio, 100.0f)
      && r.get_float("heatmap.gamma_floor", c.gamma_floor, kMinFineRatio, 100.0f)
      && r.get_float("heatmap.threshold", c.threshold, 0.0f, 1.0f)
      && r.get_bool("heatmap.block_view", c.block_view)
      && r.get_bool("heatmap.log_histogram", c.log_histogram);
}

bool read_heatmap(Reader& r, HeatMapConfig& c)
{
  return read_heatmap_state(r, c)
      && r.get_int("heatmap.legend_swatches", c.legend_swatches, 2, 4096)
      && r.get_float("heatmap.legend_height_px", c.legend_height_px, 1.0f, kMaxPixels)
      && r.get_int("heatmap.colormap_preview_steps", c.colormap_preview_steps, 2, 4096)
      && r.get_float("heatmap.colormap_preview_width_px", c.colormap_preview_width_px, 1.0f, kMaxPixels);
}

bool read_zoom(Reader& r, ZoomConfig& c)
{
  return r.get_float("view.zoom.step_below_2x", c.step_below_2x, kMinZoomStep, kMaxZoomStep)
      && r.get_float("view.zoom.step_above_2x", c.step_above_2x, kMinZoomStep, kMaxZoomStep)
      && r.get_float("view.zoom.max", c.max, 1.0f, kMaxZoom)
      && r.get_float("view.zoom.min_fit_fraction", c.min_fit_fraction, kMinRatio, 1.0f)
      && r.get_float("view.zoom.snap_epsilon", c.snap_epsilon, 0.0f, 0.5f);
}

bool read_loupe(Reader& r, LoupeConfig& c)
{
  const int max_loupe_zoom = static_cast<int>(kMaxZoom);
  return r.get_int("view.loupe.zoom", c.zoom, 1, max_loupe_zoom)
      && r.get_int("view.loupe.min_zoom", c.min_zoom, 1, max_loupe_zoom)
      && r.get_int("view.loupe.max_zoom", c.max_zoom, 1, max_loupe_zoom)
      && r.get_float("view.loupe.size_fraction", c.size_fraction, kMinRatio, 1.0f)
      && r.get_float("view.loupe.min_size_px", c.min_size_px, 1.0f, kMaxPixels)
      && r.get_float("view.loupe.max_size_px", c.max_size_px, 1.0f, kMaxPixels)
      && r.get_float("view.loupe.margin_px", c.margin_px, 0.0f, kMaxPixels)
      && r.get_float("view.loupe.cursor_avoid_px", c.cursor_avoid_px, 0.0f, kMaxPixels);
}

bool read_split(Reader& r, SplitConfig& c)
{
  return r.get_float("view.split.line_hit_px", c.line_hit_px, 0.0f, kMaxPixels)
      && r.get_float("view.split.handle_hit_px", c.handle_hit_px, 0.0f, kMaxPixels)
      && r.get_float("view.split.handle_radius_fraction", c.handle_radius_fraction, 0.0f, 1.0f)
      && r.get_float("view.split.handle_radius_max_px", c.handle_radius_max_px, 0.0f, kMaxPixels);
}

bool read_view(Reader& r, ViewConfig& c)
{
  return read_zoom(r, c.zoom)
      && read_loupe(r, c.loupe)
      && read_split(r, c.split)
      && r.get_float("view.pane_gap_px", c.pane_gap_px, 0.0f, kMaxPixels)
      && r.get_float("view.pan_drag_threshold_px", c.pan_drag_threshold_px, 0.0f, kMaxPixels)
      && r.get_float("view.canvas_min_px", c.canvas_min_px, 1.0f, kMaxPixels)
      && r.get_int("view.preview_alpha", c.preview_alpha, 0, kMaxAlpha)
      && r.get_float("view.progress_bar_width_px", c.progress_bar_width_px, 1.0f, kMaxPixels);
}

bool read_fuzzy(Reader& r, FuzzyConfig& c)
{
  return r.get_int("fuzzy.base_score", c.base_score, -kMaxScore, kMaxScore)
      && r.get_int("fuzzy.sequential_bonus", c.sequential_bonus, -kMaxScore, kMaxScore)
      && r.get_int("fuzzy.separator_bonus", c.separator_bonus, -kMaxScore, kMaxScore)
      && r.get_int("fuzzy.camel_bonus", c.camel_bonus, -kMaxScore, kMaxScore)
      && r.get_int("fuzzy.first_letter_bonus", c.first_letter_bonus, -kMaxScore, kMaxScore)
      && r.get_int("fuzzy.leading_letter_penalty", c.leading_letter_penalty, -kMaxScore, kMaxScore)
      && r.get_int("fuzzy.max_leading_letter_penalty", c.max_leading_letter_penalty, -kMaxScore, kMaxScore)
      && r.get_int("fuzzy.unmatched_letter_penalty", c.unmatched_letter_penalty, -kMaxScore, kMaxScore)
      && r.get_int("fuzzy.exact_substring_bonus", c.exact_substring_bonus, -kMaxScore, kMaxScore)
      && r.get_int("fuzzy.dropdown_rows", c.dropdown_rows, 1, kMaxDropdownRows)
      && r.get_float("fuzzy.dropdown_min_width_px", c.dropdown_min_width_px, 1.0f, kMaxPixels);
}

bool read_network(Reader& r, NetworkConfig& c)
{
  const long max_seconds = static_cast<long>(kMaxSeconds);
  return r.get_string("network.user_agent", c.user_agent, false)
      && r.get_string("network.accept", c.accept, false)
      && r.get_long("network.connect_timeout_s", c.connect_timeout_s, 1, max_seconds)
      && r.get_long("network.low_speed_limit_bytes_per_s", c.low_speed_limit_bytes_per_s, 0, static_cast<long>(kMaxCount))
      && r.get_long("network.low_speed_time_s", c.low_speed_time_s, 0, max_seconds)
      && r.get_long("network.max_redirects", c.max_redirects, 0, 100)
      && r.get_size("network.read_chunk_bytes", c.read_chunk_bytes, 1, static_cast<std::size_t>(kMaxCount))
      && r.get_int("network.winhttp.resolve_timeout_ms", c.winhttp.resolve_timeout_ms, 1, kMaxMilliseconds)
      && r.get_int("network.winhttp.connect_timeout_ms", c.winhttp.connect_timeout_ms, 1, kMaxMilliseconds)
      && r.get_int("network.winhttp.send_timeout_ms", c.winhttp.send_timeout_ms, 1, kMaxMilliseconds)
      && r.get_int("network.winhttp.receive_timeout_ms", c.winhttp.receive_timeout_ms, 1, kMaxMilliseconds);
}

bool read_process(Reader& r, ProcessConfig& c)
{
  return r.get_int("process.default_timeout_ms", c.default_timeout_ms, 1, kMaxMilliseconds)
      && r.get_int("process.probe_timeout_ms", c.probe_timeout_ms, 1, kMaxMilliseconds)
      && r.get_int("process.version_timeout_ms", c.version_timeout_ms, 1, kMaxMilliseconds)
      && r.get_int("process.dialog_timeout_ms", c.dialog_timeout_ms, 1, kMaxMilliseconds)
      && r.get_u64("process.stderr_cap_bytes", c.stderr_cap_bytes, 1, kMaxBytes)
      && r.get_u64("process.probe_stdout_cap_bytes", c.probe_stdout_cap_bytes, 1, kMaxBytes)
      && r.get_u64("process.version_stdout_cap_bytes", c.version_stdout_cap_bytes, 1, kMaxBytes)
      && r.get_u64("process.dialog_stdout_cap_bytes", c.dialog_stdout_cap_bytes, 1, kMaxBytes)
      && r.get_size("process.pipe_chunk_bytes", c.pipe_chunk_bytes, 1, static_cast<std::size_t>(kMaxCount))
      && r.get_u64("process.frame_cap_slack_bytes", c.frame_cap_slack_bytes, 0, kMaxBytes)
      && r.get_long("process.max_read_iterations", c.max_read_iterations, 1, static_cast<long>(kMaxCount));
}

bool read_ffmpeg(Reader& r, FfmpegConfig& c)
{
  return r.get_string_list("ffmpeg.search_subdirs", c.search_subdirs, kMaxSearchDirs)
      && r.get_string_list("ffmpeg.extra_search_dirs", c.extra_search_dirs, kMaxSearchDirs)
      && r.get_string("ffmpeg.scale_filter", c.scale_filter, false)
      && r.get_double("ffmpeg.seek_end_margin_s", c.seek_end_margin_s, 0.0, kMaxSeconds)
      && r.get_double("ffmpeg.fallback_frame_step_s", c.fallback_frame_step_s, kMinFrameStepS, kMaxSeconds)
      && r.get_int("ffmpeg.max_still_edge_px", c.max_still_edge_px, 1, kMaxEdgePx)
      && r.get_int("ffmpeg.sample_points", c.sample_points, 1, kMaxCount)
      && r.get_int("ffmpeg.sample_points_min", c.sample_points_min, 1, kMaxCount)
      && r.get_int("ffmpeg.sample_points_max", c.sample_points_max, 1, kMaxCount);
}

bool read_threading(Reader& r, ThreadingConfig& c)
{
  return r.get_int("threading.max_workers", c.max_workers, 1, kMaxWorkers)
      && r.get_int("threading.fallback_workers", c.fallback_workers, 1, kMaxWorkers)
      && r.get_int("threading.min_rows_per_worker", c.min_rows_per_worker, 1, kMaxCount);
}

bool read_acceleration(Reader& r, AccelerationConfig& c)
{
  return r.get_enum<OpenClMode>("acceleration.opencl", c.opencl, &opencl_mode_from_key, "auto, on, off")
      && r.get_string("acceleration.opencl_device", c.opencl_device, true)
      && r.get_long("acceleration.min_gpu_pixels", c.min_gpu_pixels, 0L, static_cast<long>(kMaxEdgePx) * kMaxEdgePx)
      && r.get_float("acceleration.self_test_tolerance", c.self_test_tolerance, kMinSelfTestTolerance, 1.0f)
      && r.get_enum<VideoBackend>("acceleration.video_backend", c.video_backend, &video_backend_from_key, "auto, opencv, ffmpeg");
}

/**
 * @brief Lower-cases ASCII letters (aliases are matched case-insensitively).
 */
std::string ascii_lower(const std::string& s)
{
  std::string out = s;
  for(std::size_t i = 0; i < out.size(); ++i)
  {
    if(out[i] >= 'A' && out[i] <= 'Z')
    {
      out[i] = static_cast<char>(out[i] - 'A' + 'a');
    }
  }
  return out;
}

/**
 * @brief Checks that a site entry only uses the four known keys.
 */
bool check_site_keys(Reader& r, const YAML::Node& entry, const std::string& path)
{
  std::size_t visited = 0;
  for(YAML::const_iterator it = entry.begin(); it != entry.end() && visited < kMaxMapEntries; ++it)
  {
    ++visited;
    if(!it->first.IsScalar())
    {
      return r.fail(path, "keys must be strings");
    }
    bool known = false;
    for(std::size_t k = 0; k < kSiteKeyCount; ++k)
    {
      known = known || (it->first.Scalar() == kSiteKeys[k]);
    }
    if(!known)
    {
      return r.fail(path + "." + it->first.Scalar(), "unknown key (a site has name, aliases, hosts and notes)");
    }
  }
  return true;
}

/**
 * @brief Reads one entry of the `sites` list.
 * @param r The reader (error sink).
 * @param entry The list element.
 * @param path Key path of the element, e.g. `sites[3]`, for messages.
 * @param limits Already-parsed limits (name length).
 * @param out Receives the site.
 * @return false (with the error recorded) on any malformed field.
 */
bool read_site(Reader& r, const YAML::Node& entry, const std::string& path, const LimitsConfig& limits, SiteInfo& out)
{
  if(!entry.IsDefined() || !entry.IsMap())
  {
    return r.fail(path, "expected a map with name, aliases, hosts and notes");
  }
  if(!check_site_keys(r, entry, path))
  {
    return false;
  }
  const YAML::Node name = child_of(entry, "name");
  if(!name.IsDefined() || !name.IsScalar() || name.Scalar().empty())
  {
    return r.fail(path + ".name", "required, must be a non-empty string");
  }
  if(name.Scalar().size() > static_cast<std::size_t>(limits.max_site_name_chars))
  {
    return r.fail(path + ".name", "longer than limits.max_site_name_chars");
  }
  SiteInfo site;
  site.name = name.Scalar();
  const YAML::Node aliases = child_of(entry, "aliases");
  if(aliases.IsDefined() && !r.read_string_list(aliases, path + ".aliases", site.aliases, kMaxSiteStrings))
  {
    return false;
  }
  const YAML::Node hosts = child_of(entry, "hosts");
  if(hosts.IsDefined() && !r.read_string_list(hosts, path + ".hosts", site.hosts, kMaxSiteStrings))
  {
    return false;
  }
  const YAML::Node notes = child_of(entry, "notes");
  if(notes.IsDefined())
  {
    if(!notes.IsScalar())
    {
      return r.fail(path + ".notes", "expected a string");
    }
    site.notes = notes.Scalar();
  }
  for(std::size_t i = 0; i < site.aliases.size() && i < kMaxSiteStrings; ++i)
  {
    site.aliases[i] = ascii_lower(site.aliases[i]);
  }
  for(std::size_t i = 0; i < site.hosts.size() && i < kMaxSiteStrings; ++i)
  {
    site.hosts[i] = ascii_lower(site.hosts[i]);
  }
  out = std::move(site);
  return true;
}

/**
 * @brief Reads the `sites` list and rejects duplicate names.
 */
bool read_sites(Reader& r, const LimitsConfig& limits, std::vector<SiteInfo>& out)
{
  YAML::Node list;
  if(!r.find("sites", list))
  {
    return false;
  }
  r.consume_subtree("sites");
  if(!list.IsSequence() || list.size() == 0)
  {
    return r.fail("sites", "expected a non-empty list");
  }
  const std::size_t count = list.size();
  if(count > kMaxSites)
  {
    return r.fail("sites", "more than " + std::to_string(kMaxSites) + " entries");
  }
  std::vector<SiteInfo> sites;
  sites.reserve(count);
  std::set<std::string> names;
  for(std::size_t i = 0; i < count && i < kMaxSites; ++i)
  {
    const std::string path = "sites[" + std::to_string(i) + "]";
    SiteInfo site;
    if(!read_site(r, list[i], path, limits, site))
    {
      return false;
    }
    if(!names.insert(ascii_lower(site.name)).second)
    {
      return r.fail(path + ".name", "duplicate site name '" + site.name + "'");
    }
    sites.push_back(std::move(site));
  }
  out = std::move(sites);
  return true;
}

/**
 * @brief Rules that involve more than one key.
 */
bool check_cross_field(Reader& r, const Config& c)
{
  if(c.metrics.ssim.window % 2 == 0)
  {
    return r.fail("metrics.ssim.window", "must be odd");
  }
  if(c.metrics.histogram_fine_bins < c.metrics.histogram_bins)
  {
    return r.fail("metrics.histogram_fine_bins", "must be at least metrics.histogram_bins");
  }
  if(c.heatmap.gamma_min > c.heatmap.gamma_max || c.heatmap.gamma < c.heatmap.gamma_min || c.heatmap.gamma > c.heatmap.gamma_max)
  {
    return r.fail("heatmap.gamma", "must lie within [heatmap.gamma_min, heatmap.gamma_max]");
  }
  if(c.heatmap.manual_max_min_factor >= c.heatmap.manual_max_max_factor)
  {
    return r.fail("heatmap.manual_max_min_factor", "must be smaller than heatmap.manual_max_max_factor");
  }
  if(c.view.loupe.min_zoom > c.view.loupe.max_zoom || c.view.loupe.zoom < c.view.loupe.min_zoom || c.view.loupe.zoom > c.view.loupe.max_zoom)
  {
    return r.fail("view.loupe.zoom", "must lie within [view.loupe.min_zoom, view.loupe.max_zoom]");
  }
  if(c.view.loupe.min_size_px > c.view.loupe.max_size_px)
  {
    return r.fail("view.loupe.min_size_px", "must not exceed view.loupe.max_size_px");
  }
  if(c.app.panels.slot_thumb_min_px > c.app.panels.slot_thumb_max_px)
  {
    return r.fail("app.panels.slot_thumb_min_px", "must not exceed app.panels.slot_thumb_max_px");
  }
  if(c.ffmpeg.sample_points_min > c.ffmpeg.sample_points_max || c.ffmpeg.sample_points < c.ffmpeg.sample_points_min
     || c.ffmpeg.sample_points > c.ffmpeg.sample_points_max)
  {
    return r.fail("ffmpeg.sample_points", "must lie within [ffmpeg.sample_points_min, ffmpeg.sample_points_max]");
  }
  if(c.threading.fallback_workers > c.threading.max_workers)
  {
    return r.fail("threading.fallback_workers", "must not exceed threading.max_workers");
  }
  return true;
}

/**
 * @brief Reads a complete document into a Config.
 */
bool read_config(const YAML::Node& root, const std::string& source, Config& out, std::string& err)
{
  Reader r(root, source);
  Config cfg;
  if(!root.IsDefined() || !root.IsMap())
  {
    err = source + ": document: expected a map of sections";
    return false;
  }
  const bool ok = r.get_int("schema_version", cfg.schema_version, kConfigSchemaVersion, kConfigSchemaVersion)
               && read_app(r, cfg.app) && read_limits(r, cfg.limits) && read_metrics(r, cfg.metrics)
               && read_heatmap(r, cfg.heatmap) && read_view(r, cfg.view) && read_fuzzy(r, cfg.fuzzy)
               && read_network(r, cfg.network) && read_process(r, cfg.process) && read_ffmpeg(r, cfg.ffmpeg)
               && read_threading(r, cfg.threading) && read_acceleration(r, cfg.acceleration) && read_sites(r, cfg.limits, cfg.sites)
               && r.check_unknown_keys() && check_cross_field(r, cfg);
  if(!ok)
  {
    err = r.error();
    return false;
  }
  out = std::move(cfg);
  return true;
}

/* ---- merging ----------------------------------------------------------- */

/**
 * @brief Finds the index of the site with a given name in a YAML list.
 * @return true when found; `index` receives the position.
 */
bool find_site_index(const YAML::Node& sites, const std::string& name, std::size_t& index)
{
  const std::size_t count = sites.size();
  for(std::size_t i = 0; i < count && i < kMaxSites; ++i)
  {
    const YAML::Node entry = sites[i];
    if(entry.IsDefined() && entry.IsMap())
    {
      const YAML::Node entry_name = child_of(entry, "name");
      if(entry_name.IsDefined() && entry_name.IsScalar() && ascii_lower(entry_name.Scalar()) == ascii_lower(name))
      {
        index = i;
        return true;
      }
    }
  }
  return false;
}

/**
 * @brief Copies every field of `over` into `base` (one level; site fields are leaves).
 */
bool overwrite_fields(YAML::Node base, const YAML::Node& over)
{
  std::size_t visited = 0;
  for(YAML::const_iterator it = over.begin(); it != over.end() && visited < kMaxMapEntries; ++it)
  {
    ++visited;
    if(!it->first.IsScalar())
    {
      return false;
    }
    base[it->first.Scalar()] = it->second;
  }
  return true;
}

/**
 * @brief Merges an override `sites` list into the base list by name.
 *
 * An entry whose name matches (case-insensitively) updates the fields it lists;
 * a new name is appended.
 */
bool merge_sites(YAML::Node base_sites, const YAML::Node& over_sites, const std::string& source, std::string& err)
{
  if(!over_sites.IsDefined() || !over_sites.IsSequence())
  {
    err = source + ": sites: expected a list";
    return false;
  }
  const std::size_t count = over_sites.size();
  if(count > kMaxSites)
  {
    err = source + ": sites: more than " + std::to_string(kMaxSites) + " entries";
    return false;
  }
  for(std::size_t i = 0; i < count && i < kMaxSites; ++i)
  {
    const YAML::Node entry = over_sites[i];
    const std::string path = source + ": sites[" + std::to_string(i) + "]";
    if(!entry.IsDefined() || !entry.IsMap())
    {
      err = path + ": expected a map";
      return false;
    }
    const YAML::Node name = child_of(entry, "name");
    if(!name.IsDefined() || !name.IsScalar() || name.Scalar().empty())
    {
      err = path + ".name: required, must be a non-empty string";
      return false;
    }
    std::size_t index = 0;
    if(find_site_index(base_sites, name.Scalar(), index))
    {
      if(!overwrite_fields(base_sites[index], entry))
      {
        err = path + ": keys must be strings";
        return false;
      }
    }
    else
    {
      base_sites.push_back(entry);
    }
  }
  return true;
}

/**
 * @brief Deep-merges an override document into the base document.
 *
 * Maps merge key by key, lists and scalars replace, and the top-level `sites`
 * list merges by name.  Iterative (an explicit work list) so that nesting is
 * bounded and no recursion is needed.
 */
bool merge_documents(const YAML::Node& base, const YAML::Node& over, const std::string& source, std::string& err)
{
  struct Item
  {
    YAML::Node base;
    YAML::Node over;
    int depth;
  };
  if(!over.IsDefined() || !over.IsMap())
  {
    err = source + ": document: expected a map of sections";
    return false;
  }
  std::vector<Item> work;
  work.push_back(Item{base, over, 0});
  std::size_t visited = 0;
  while(!work.empty() && visited < kMaxMapEntries)
  {
    const Item item = work.back();
    work.pop_back();
    if(item.depth > kMaxDepth)
    {
      err = source + ": document: nested too deeply";
      return false;
    }
    for(YAML::const_iterator it = item.over.begin(); it != item.over.end() && visited < kMaxMapEntries; ++it)
    {
      ++visited;
      if(!it->first.IsScalar())
      {
        err = source + ": document: keys must be strings";
        return false;
      }
      const std::string key = it->first.Scalar();
      YAML::Node target = item.base;
      const YAML::Node existing = child_of(target, key);
      if(item.depth == 0 && key == "sites" && existing.IsDefined() && existing.IsSequence())
      {
        if(!merge_sites(target[key], it->second, source, err))
        {
          return false;
        }
      }
      else if(existing.IsDefined() && existing.IsMap() && it->second.IsDefined() && it->second.IsMap())
      {
        work.push_back(Item{target[key], it->second, item.depth + 1});
      }
      else
      {
        target[key] = it->second;
      }
    }
  }
  if(!work.empty())
  {
    err = source + ": document: too many entries";
    return false;
  }
  return true;
}

/**
 * @brief Reads a whole file into a string, refusing files over kMaxConfigFileBytes.
 */
bool read_text_file(const std::string& path, std::string& out, std::string& err)
{
  std::error_code ec;
  const std::uintmax_t size = std::filesystem::file_size(std::filesystem::u8path(path), ec);
  if(ec)
  {
    err = path + ": cannot read (" + ec.message() + ")";
    return false;
  }
  if(size > kMaxConfigFileBytes)
  {
    err = path + ": larger than " + std::to_string(kMaxConfigFileBytes) + " bytes";
    return false;
  }
  std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
  if(!in)
  {
    err = path + ": cannot open";
    return false;
  }
  std::string text(static_cast<std::size_t>(size), '\0');
  if(size > 0 && !in.read(&text[0], static_cast<std::streamsize>(size)))
  {
    err = path + ": read failed";
    return false;
  }
  out = std::move(text);
  return true;
}

/**
 * @brief Checks the `schema_version` an override file declares, if any.
 */
bool check_override_schema(const YAML::Node& over, const std::string& source, std::string& err)
{
  if(!over.IsDefined() || !over.IsMap())
  {
    err = source + ": document: expected a map of sections";
    return false;
  }
  const YAML::Node version = child_of(over, "schema_version");
  int declared = kConfigSchemaVersion;
  if(version.IsDefined() && (!version.IsScalar() || !YAML::convert<int>::decode(version, declared)))
  {
    err = source + ": schema_version: expected an integer";
    return false;
  }
  if(declared != kConfigSchemaVersion)
  {
    err = source + ": schema_version: this build understands version " + std::to_string(kConfigSchemaVersion);
    return false;
  }
  return true;
}

/**
 * @brief Loads the built-in document, merges an override and reads the result.
 */
bool load_config_impl(const std::string& override_path, Config& out, std::string& err)
{
  YAML::Node base = YAML::Load(builtin_config_yaml());
  if(override_path.empty())
  {
    return read_config(base, "built-in config.yaml", out, err);
  }
  std::string text;
  if(!read_text_file(override_path, text, err))
  {
    return false;
  }
  const YAML::Node over = YAML::Load(text);
  if(!check_override_schema(over, override_path, err) || !merge_documents(base, over, override_path, err))
  {
    return false;
  }
  return read_config(base, override_path, out, err);
}

/**
 * @brief True when `path` names an existing regular file (no exceptions).
 */
bool file_exists(const std::string& path)
{
  std::error_code ec;
  const bool exists = std::filesystem::is_regular_file(std::filesystem::u8path(path), ec);
  return !ec && exists;
}

} // namespace

const char* builtin_config_yaml()
{
  static_assert(generated::kBuiltinConfigYaml_size > 0, "config/config.yaml must not be empty");
  return reinterpret_cast<const char*>(generated::kBuiltinConfigYaml);
}

bool parse_config(const std::string& yaml_text, const std::string& source, Config& out, std::string& err)
{
  try
  {
    const YAML::Node root = YAML::Load(yaml_text);
    return read_config(root, source, out, err);
  }
  catch(const std::exception& e)
  {
    err = source + ": " + e.what();
    return false;
  }
}

bool load_config(const std::string& override_path, Config& out, std::string& err)
{
  try
  {
    return load_config_impl(override_path, out, err);
  }
  catch(const std::exception& e)
  {
    err = (override_path.empty() ? std::string("built-in config.yaml") : override_path) + ": " + e.what();
    return false;
  }
}

const Config& default_config()
{
  static const Config config = []() {
    Config c;
    std::string err;
    const bool ok = load_config(std::string(), c, err);
    CC_ENSURE(ok && err.empty());
    if(!ok)
    {
      std::fprintf(stderr, "%s\n", err.c_str());
    }
    return c;
  }();
  return config;
}

std::string user_config_path()
{
#ifdef _WIN32
  return std::string();
#else
  const char* home = std::getenv("HOME");
#ifdef __APPLE__
  /* Inside an .app bundle the executable directory is not the place to edit files. */
  return home != nullptr ? std::string(home) + "/Library/Application Support/CompressCompare/config.yaml" : std::string();
#else
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  if(xdg != nullptr && xdg[0] != '\0')
  {
    return std::string(xdg) + "/compresscompare/config.yaml";
  }
  return home != nullptr ? std::string(home) + "/.config/compresscompare/config.yaml" : std::string();
#endif
#endif
}

std::string find_config_override(const std::string& explicit_path, const std::string& exe_dir)
{
  if(!explicit_path.empty())
  {
    return explicit_path;
  }
  if(!exe_dir.empty())
  {
    const std::string beside_exe = exe_dir + "/config.yaml";
    if(file_exists(beside_exe))
    {
      return beside_exe;
    }
  }
  std::string user = user_config_path();
  if(!user.empty() && file_exists(user))
  {
    return user;
  }
  const std::string in_cwd = "config.yaml";
  return file_exists(in_cwd) ? in_cwd : std::string();
}

} // namespace cc
