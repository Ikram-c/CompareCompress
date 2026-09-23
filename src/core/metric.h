/**
 * @file metric.h
 * @brief The per-pixel difference metrics the heat map can show.
 */
#pragma once

#include <string>

namespace cc {

/** @brief Per-pixel difference measure between the original and the site copy. */
enum class Metric
{
  AbsDiff,      /**< max over R, G, B of |a - b|, in 8-bit levels (0..255) */
  LumaDiff,     /**< |Y_a - Y_b| with Rec.601 luma, in levels (0..255) */
  DeltaE76,     /**< Euclidean distance in CIELAB (0..~100) */
  DeltaE2000,   /**< CIEDE2000 (0..~100) */
  SSIM,         /**< 1 - SSIM on luma over a Gaussian window (0..1) */
  SquaredError, /**< mean over R, G, B of (a - b)^2 (0..65025) */
  COUNT         /**< number of metrics; not a metric */
};

/** @brief Number of real metrics (excludes Metric::COUNT). */
constexpr int kMetricCount = static_cast<int>(Metric::COUNT);

/** @brief Human-readable name shown in the UI. */
const char* metric_name(Metric m);

/** @brief Unit label for the metric's values. */
const char* metric_unit(Metric m);

/** @brief Multi-line tooltip explaining the metric. */
const char* metric_help(Metric m);

/** @brief Configuration key of the metric (`abs_diff`, `delta_e2000`, ...). */
const char* metric_key(Metric m);

/**
 * @brief Parses a configuration key into a metric.
 * @param key Key as written in config.yaml.
 * @param out Receives the metric on success.
 * @return true when the key names a metric.
 */
bool metric_from_key(const std::string& key, Metric& out);

} // namespace cc
