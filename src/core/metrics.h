/**
 * @file metrics.h
 * @brief Per-pixel difference maps ("heat maps") and summary statistics
 * between an original image and the version a site served back.
 *
 * The pixel-wise kernels are ports of CLIJ2 OpenCL kernels
 * (absolute_difference, squared_difference, mean_squared_error,
 * gaussian_blur_separable, mean/variance for the SSIM window, min/max/mean
 * reductions); see docs/explanation/references.md.  They run as OpenCL
 * kernels through OpenCV's Transparent API when OpenCL is enabled and on the
 * CPU otherwise (core/cv_ops.h); filtering and reductions are OpenCV's
 * (cv::GaussianBlur, cv::mean, cv::minMaxLoc, cv::countNonZero).  The colour maths (sRGB
 * transfer curve, XYZ matrix, Lab, dE76 / dE2000) follows Lindbloom, the CIE
 * and Sharma 2005.
 */
#pragma once

#include "core/config.h"
#include "core/image.h"
#include "core/metric.h"
#include "core/threading.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cc {

/** @brief A CIELAB colour (D65). */
struct Lab
{
  float L;
  float a;
  float b;
};

/** @brief The 256-entry 8-bit sRGB to linear-light table (IEC 61966-2-1). */
[[nodiscard]] const float* srgb_to_linear_table();

/** @brief Converts an 8-bit sRGB pixel to CIELAB. */
[[nodiscard]] Lab srgb8_to_lab(uint8_t r, uint8_t g, uint8_t b);

/** @brief CIE76 colour difference (Euclidean distance in Lab). */
[[nodiscard]] float delta_e76(const Lab& x, const Lab& y);

/** @brief CIEDE2000 colour difference, Sharma, Wu & Dalal (2005) formulation, kL = kC = kH = 1. */
[[nodiscard]] float delta_e2000(const Lab& x, const Lab& y);

/** @brief Rec.601 luma of an 8-bit pixel, in levels (0..255). */
[[nodiscard]] float luma601(uint8_t r, uint8_t g, uint8_t b);

/** @brief A per-pixel metric map with its statistics. */
struct HeatMap
{
  Metric metric = Metric::AbsDiff;
  int w = 0;
  int h = 0;
  std::vector<float> v; /**< raw metric values, row-major */
  float min = 0;
  float max = 0;
  float mean = 0;
  float p50 = 0;
  float p95 = 0;
  float p99 = 0;
  float p_auto = 0;            /**< value at `metrics.auto_scale_percentile`, used by the auto colour scale */
  std::vector<uint32_t> hist;  /**< `metrics.histogram_bins` bins over [0, max] */
  bool block_averaged = false; /**< block_average() was applied */

  /** @brief True when the dimensions and the buffer agree and are non-empty. */
  [[nodiscard]] bool valid() const
  {
    return w > 0 && h > 0 && v.size() == static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
  }

  /** @brief Value at (x, y); no bounds check. */
  [[nodiscard]] float at(int x, int y) const
  {
    return v[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)];
  }

  /**
   * @brief Recomputes min, max, mean, the percentiles and the histogram.
   * @param cfg `metrics` section (histogram_bins, histogram_fine_bins).
   */
  void finalize(const MetricsConfig& cfg);
};

/** @brief Image-wide statistics of a pair. */
struct PairStats
{
  bool ok = false;
  double mse = 0;           /**< over R, G, B */
  double psnr = 0;          /**< dB; `metrics.psnr_identical_db` for identical images */
  double ssim = 0;          /**< mean SSIM on luma */
  double mean_abs = 0;      /**< mean of Metric::AbsDiff */
  double mean_de2000 = 0;
  double max_de2000 = 0;
  double pct_over_jnd = 0;  /**< % of pixels above `metrics.jnd_delta_e2000` */
  double pct_identical = 0; /**< % of pixels with zero difference */
};

/**
 * @brief Computes a heat map.
 * @param a Original.
 * @param b Site copy; must have the same dimensions as `a`.
 * @param m The metric.
 * @param cfg `metrics` section.
 * @param threading `threading` section.
 * @param progress Optional progress sink.
 * @return An invalid map when the inputs do not match.
 */
[[nodiscard]] HeatMap compute_heatmap(const Image& a, const Image& b, Metric m, const MetricsConfig& cfg,
                                      const ThreadingConfig& threading, Progress* progress = nullptr);

/**
 * @brief Replaces every aligned block x block cell by its mean (JPEG 8x8 grid view).
 * @param hm The map, modified in place and re-finalised.
 * @param block Cell size in pixels (`metrics.block_size_px`); values below 2 do nothing.
 * @param cfg `metrics` section for finalize().
 */
void block_average(HeatMap& hm, int block, const MetricsConfig& cfg);

/**
 * @brief Computes image-wide statistics.
 * @param a Original.
 * @param b Site copy; must have the same dimensions as `a`.
 * @param cfg `metrics` section.
 * @param threading `threading` section.
 * @param progress Optional progress sink.
 */
[[nodiscard]] PairStats compute_stats(const Image& a, const Image& b, const MetricsConfig& cfg, const ThreadingConfig& threading,
                                      Progress* progress = nullptr);

/**
 * @brief Separable Gaussian blur (cv::GaussianBlur, edge-clamped like CLIJ2 gaussian_blur_separable).
 * @param src w * h values.
 * @param dst Receives w * h values.
 * @param w Width.
 * @param h Height.
 * @param window Kernel size in pixels (odd).
 * @param sigma Gaussian sigma in pixels.
 * @param threading `threading` section.
 */
void gaussian_blur_separable(const std::vector<float>& src, std::vector<float>& dst, int w, int h, int window, float sigma,
                             const ThreadingConfig& threading);

} // namespace cc
