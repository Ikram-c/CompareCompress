/**
 * @file image.h
 * @brief 8-bit RGBA images: file I/O, content-based type sniffing, decoding,
 * resampling and PNG export.
 *
 * Design notes (see docs/explanation/references.md):
 *  - Type detection is by content, not extension, like darktable's signature
 *    table: sites routinely serve WebP under a `.jpg` name.
 *  - Decoding goes through OpenCV's imgcodecs (bundled libjpeg-turbo,
 *    libpng, libwebp, libtiff, GIF and BMP readers); on macOS, ImageIO
 *    handles what OpenCV cannot (HEIC/HEIF from iPhones, AVIF on 13+).
 *    Header sizes are checked before decoding where they are cheap to read.
 *  - JPEGs are decoded with their EXIF orientation applied, which is what
 *    sites do before re-encoding.
 *  - Resampling converts to linear light with premultiplied alpha, uses
 *    cv::resize (INTER_AREA when shrinking) and converts back; it runs
 *    through OpenCL when that is enabled.
 */
#pragma once

#include "core/config.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cc {

/** @brief Bytes per pixel of every Image (R, G, B, A). */
constexpr int kImageChannels = 4;

/** @brief Value of an opaque alpha byte. */
constexpr uint8_t kOpaqueAlpha = 255;

/** @brief An 8-bit sRGB image with alpha, top-left origin, row-major. */
struct Image
{
  int w = 0;
  int h = 0;
  std::vector<uint8_t> rgba; /**< w * h * kImageChannels bytes */

  /** @brief True when the dimensions and the buffer agree and are non-empty. */
  [[nodiscard]] bool valid() const
  {
    return w > 0 && h > 0 && rgba.size() == static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * kImageChannels;
  }

  /** @brief Pointer to the four bytes of pixel (x, y); no bounds check. */
  [[nodiscard]] const uint8_t* px(int x, int y) const
  {
    return &rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * kImageChannels];
  }

  /** @copydoc px(int, int) const */
  [[nodiscard]] uint8_t* px(int x, int y)
  {
    return &rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * kImageChannels];
  }

  /**
   * @brief An image filled with one byte value in every channel.
   * @param w Width in pixels (>= 1).
   * @param h Height in pixels (>= 1).
   * @param v Fill value.
   */
  [[nodiscard]] static Image blank(int w, int h, uint8_t v = 0);
};

/** @brief What the first bytes of a file say it is. */
enum class FileKind
{
  Unknown,
  JPEG,
  PNG,
  WebP,
  GIF,
  BMP,
  AVIF,
  HEIF,
  TIFF,
  Video,
  HTML,
  Other
};

/** @brief Display name of a file kind ("JPEG", "web page", ...). */
[[nodiscard]] const char* file_kind_name(FileKind k);

/** @brief True for the still-image kinds. */
[[nodiscard]] bool file_kind_is_image(FileKind k);

/**
 * @brief Content-based type detection (magic numbers).
 * @param data First bytes of the file.
 * @param n Number of bytes available.
 * @param hint_ext Lower-case extension without the dot; only consulted for
 *        video containers that have no reliable signature.
 */
[[nodiscard]] FileKind sniff_bytes(const uint8_t* data, std::size_t n, const std::string& hint_ext = "");

/**
 * @brief Reads a whole file.
 * @param path UTF-8 path.
 * @param out Receives the bytes.
 * @param err Receives a message on failure.
 * @param max_bytes Refuse files larger than this; 0 = no cap.
 * @return true on success.
 */
[[nodiscard]] bool read_file(const std::string& path, std::vector<uint8_t>& out, std::string& err, uint64_t max_bytes = 0);

/**
 * @brief Reads the first `n` bytes of a file (fewer if it is shorter).
 * @return true when the file could be opened.
 */
[[nodiscard]] bool read_file_head(const std::string& path, std::size_t n, std::vector<uint8_t>& out, std::string& err);

/** @brief Size of a file; false when it cannot be stat'ed. */
[[nodiscard]] bool file_size(const std::string& path, uint64_t& size);

/** @brief True when `path` is an existing regular file. */
[[nodiscard]] bool file_exists(const std::string& path);

/** @brief Writes bytes to a file (UTF-8 path), replacing it. */
[[nodiscard]] bool write_file(const std::string& path, const uint8_t* data, std::size_t n, std::string& err);

/** @brief Outcome of a decode. */
struct LoadResult
{
  bool ok = false;
  std::string error;
  FileKind kind = FileKind::Unknown;
  std::string decoder; /**< "OpenCV imgcodecs" or "macOS ImageIO" */
};

/**
 * @brief Decodes an image held in memory.
 * @param data Encoded bytes.
 * @param n Number of bytes.
 * @param max_edge_px Refuse images whose width or height exceeds this (`limits.max_image_edge_px`).
 * @param out Receives the pixels on success.
 * @param hint_ext Lower-case extension for the sniffer.
 */
[[nodiscard]] LoadResult decode_image(const uint8_t* data, std::size_t n, int max_edge_px, Image& out,
                                      const std::string& hint_ext = "");

/**
 * @brief Reads and decodes an image file within the configured limits.
 * @param path UTF-8 path.
 * @param limits `limits` section (max_image_bytes, max_image_edge_px).
 * @param out Receives the pixels on success.
 */
[[nodiscard]] LoadResult load_image_file(const std::string& path, const LimitsConfig& limits, Image& out);

/**
 * @brief High-quality resample to exactly w x h (aspect is NOT preserved; callers crop first).
 * @return An empty image when the input or the size is invalid.
 */
[[nodiscard]] Image resample(const Image& src, int w, int h);

/** @brief Crops a rectangle, clamped to the source bounds. */
[[nodiscard]] Image crop(const Image& src, int x, int y, int w, int h);

/**
 * @brief Scales to cover w x h, then centre-crops (what most sites do when they force an aspect ratio).
 * @param src The source image.
 * @param w Target width in pixels.
 * @param h Target height in pixels.
 * @param offset_x Shift of the crop window from the centre, clamped.
 * @param offset_y Shift of the crop window from the centre, clamped.
 * @return The w x h image, or an empty image for invalid input.
 */
[[nodiscard]] Image fit_cover(const Image& src, int w, int h, int offset_x = 0, int offset_y = 0);

/** @brief Writes an image as PNG. */
[[nodiscard]] bool save_png(const Image& img, const std::string& path, std::string& err);

} // namespace cc
