/**
 * @file imageio_apple.h
 * @brief Still-image decoding through macOS ImageIO (HEIC/HEIF from iPhones,
 * AVIF on macOS 13 and later, and anything else the system can read).
 *
 * Only compiled on Apple platforms.  Uses the CoreFoundation / ImageIO /
 * CoreGraphics C APIs, so no Objective-C is involved.  The image is decoded
 * with its EXIF/HEIF orientation applied and converted to sRGB (iPhone photos
 * are Display P3), matching what a site produces before re-encoding.
 */
#pragma once

#include "core/image.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace cc {

/**
 * @brief Decodes an image held in memory with ImageIO.
 * @param data Encoded bytes.
 * @param n Number of bytes.
 * @param max_edge_px Refuse images whose width or height exceeds this.
 * @param out Receives 8-bit sRGB RGBA (straight alpha) on success.
 * @param err Receives a message on failure.
 */
[[nodiscard]] bool decode_with_imageio(const uint8_t* data, std::size_t n, int max_edge_px, Image& out, std::string& err);

} // namespace cc
