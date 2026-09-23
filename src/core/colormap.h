/**
 * @file colormap.h
 * @brief Colour maps for the heat map: piecewise-linear ramps evaluated on the
 * CPU (legend, tooltips, PNG export) and baked into a 256-entry table for the GPU.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cc {

/** @brief Available colour maps. */
enum class ColorMap
{
  Inferno, /**< perceptual, black -> purple -> orange -> yellow (best on dark images) */
  Viridis, /**< perceptual, purple -> teal -> yellow */
  Heat,    /**< classic black -> red -> yellow -> white */
  Ice,     /**< blue ramp, good over warm photos */
  Alert,   /**< dark -> pure red (single hue, overlay use) */
  Gray,    /**< black -> white */
  COUNT    /**< number of maps; not a map */
};

/** @brief Number of real colour maps (excludes ColorMap::COUNT). */
constexpr int kColorMapCount = static_cast<int>(ColorMap::COUNT);

/** @brief Number of entries in the GPU lookup table produced by colormap_table(). */
constexpr int kColorMapTableSize = 256;

/** @brief Number of colour channels per table entry (R, G, B). */
constexpr int kColorChannels = 3;

/** @brief Human-readable name shown in the UI. */
const char* colormap_name(ColorMap c);

/** @brief Configuration key of the map (`inferno`, `viridis`, ...). */
const char* colormap_key(ColorMap c);

/**
 * @brief Parses a configuration key into a colour map.
 * @param key Key as written in config.yaml.
 * @param out Receives the map on success.
 * @return true when the key names a map.
 */
bool colormap_from_key(const std::string& key, ColorMap& out);

/**
 * @brief Evaluates a colour map.
 * @param c The map.
 * @param t Position along the ramp, clamped to [0, 1].
 * @param rgb Receives red, green and blue in [0, 1].
 */
void colormap_eval(ColorMap c, float t, float rgb[kColorChannels]);

/**
 * @brief Bakes a colour map into kColorMapTableSize RGB8 entries for a GL_RGB8 texture.
 * @return kColorMapTableSize * kColorChannels bytes.
 */
std::vector<uint8_t> colormap_table(ColorMap c);

} // namespace cc
