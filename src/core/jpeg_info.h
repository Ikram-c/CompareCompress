/**
 * @file jpeg_info.h
 * @brief Reads the parts of a JPEG that tell you *how* a site re-encoded it.
 *
 * Quantisation tables (which give an estimated IJG quality), chroma
 * subsampling, progressive versus baseline, and the presence of EXIF, ICC,
 * JFIF and Adobe markers.  The quality estimate inverts the IJG
 * `jpeg_quality_scaling()` formula from libjpeg's jcparam.c.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace cc {

/** @brief Value of JpegInfo::quality_* when no estimate could be made. */
constexpr int kJpegQualityUnknown = -1;

/** @brief What the JPEG headers say about the encoder. */
struct JpegInfo
{
  bool ok = false; /**< a frame header was found */
  int width = 0;
  int height = 0;
  int components = 0;
  bool progressive = false;
  std::string subsampling; /**< "4:4:4", "4:2:2", "4:2:0", "4:4:0", "4:1:1", "gray" or "?" */
  bool has_exif = false;
  bool has_icc = false;
  bool has_adobe = false;
  bool has_jfif = false;
  int quant_tables = 0;                    /**< DQT tables seen */
  int quality_luma = kJpegQualityUnknown;   /**< estimated IJG quality 1..100 from the luma table */
  int quality_chroma = kJpegQualityUnknown; /**< ... from the chroma table */
  int quality = kJpegQualityUnknown;        /**< combined estimate */
  float table_fit_error = 0.0f;             /**< mean absolute deviation (%) from the closest scaled IJG table */
  int scans = 0;                            /**< SOS markers (progressive files have several) */

  /**
   * @brief One-line description, e.g. "progressive JPEG, 4:2:0, q~82".
   * @param custom_table_threshold_pct Fit error above which the tables are called custom
   *        (`metrics.custom_table_fit_error_pct`).
   */
  [[nodiscard]] std::string summary(float custom_table_threshold_pct) const;
};

/**
 * @brief Parses the marker segments of a JPEG held in memory.
 * @param data The file bytes.
 * @param n Number of bytes.
 * @return `ok == false` when the data is not a JPEG or has no frame header.
 */
[[nodiscard]] JpegInfo parse_jpeg_info(const uint8_t* data, std::size_t n);

} // namespace cc
