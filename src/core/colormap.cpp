/**
 * @file colormap.cpp
 * @brief Piecewise-linear colour ramps.
 *
 * The anchor colours are sampled from the matplotlib maps (viridis and
 * inferno are released under CC0); linear interpolation between anchors is
 * plenty for a heat map.  The anchors are data, not settings, so they live
 * here rather than in config.yaml.
 */
#include "core/colormap.h"

#include "util/contract.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace cc {

namespace {

/** @brief One anchor of a ramp: position along [0, 1] and its colour. */
struct Stop
{
  float t;
  float r;
  float g;
  float b;
};

/** @brief Largest number of anchors any ramp has (bounds the search loop). */
constexpr std::size_t kMaxStops = 16;

/** @brief Value of the brightest 8-bit level. */
constexpr float kMaxLevel = 255.0f;

const Stop kInferno[] = {{0.00f, 0.001f, 0.000f, 0.014f}, {0.13f, 0.098f, 0.033f, 0.234f}, {0.25f, 0.259f, 0.039f, 0.406f},
                         {0.38f, 0.425f, 0.099f, 0.432f}, {0.50f, 0.578f, 0.148f, 0.404f}, {0.63f, 0.735f, 0.215f, 0.330f},
                         {0.75f, 0.865f, 0.316f, 0.226f}, {0.88f, 0.955f, 0.485f, 0.083f}, {0.95f, 0.988f, 0.647f, 0.040f},
                         {1.00f, 0.988f, 0.998f, 0.645f}};
const Stop kViridis[] = {{0.00f, 0.267f, 0.005f, 0.329f}, {0.13f, 0.282f, 0.140f, 0.458f}, {0.25f, 0.254f, 0.265f, 0.530f},
                         {0.38f, 0.207f, 0.372f, 0.553f}, {0.50f, 0.164f, 0.471f, 0.558f}, {0.63f, 0.128f, 0.567f, 0.551f},
                         {0.75f, 0.135f, 0.659f, 0.518f}, {0.88f, 0.267f, 0.749f, 0.441f}, {0.95f, 0.478f, 0.821f, 0.318f},
                         {1.00f, 0.993f, 0.906f, 0.144f}};
const Stop kHeat[] = {{0.00f, 0.0f, 0.0f, 0.0f}, {0.35f, 0.75f, 0.05f, 0.0f}, {0.70f, 1.0f, 0.75f, 0.0f}, {1.00f, 1.0f, 1.0f, 1.0f}};
const Stop kIce[] = {{0.00f, 0.02f, 0.02f, 0.10f}, {0.40f, 0.05f, 0.30f, 0.75f}, {0.75f, 0.30f, 0.80f, 1.0f}, {1.00f, 1.0f, 1.0f, 1.0f}};
const Stop kAlert[] = {{0.00f, 0.15f, 0.0f, 0.0f}, {1.00f, 1.0f, 0.05f, 0.05f}};
const Stop kGray[] = {{0.00f, 0.0f, 0.0f, 0.0f}, {1.00f, 1.0f, 1.0f, 1.0f}};

/**
 * @brief Interpolates a ramp.
 * @param stops Anchors sorted by t, at least two.
 * @param count Number of anchors.
 * @param t Position, clamped to [0, 1].
 * @param rgb Receives the colour.
 */
void eval_stops(const Stop* stops, std::size_t count, float t, float rgb[kColorChannels])
{
  CC_REQUIRE(stops != nullptr && count >= 2 && count <= kMaxStops, return);
  CC_REQUIRE(rgb != nullptr, return);
  const float u_clamped = std::clamp(t, 0.0f, 1.0f);
  std::size_t i = 0;
  while(i + 2 < count && u_clamped > stops[i + 1].t)
  {
    ++i;
  }
  const Stop& a = stops[i];
  const Stop& b = stops[i + 1];
  const float u = (b.t > a.t) ? (u_clamped - a.t) / (b.t - a.t) : 0.0f;
  rgb[0] = a.r + (b.r - a.r) * u;
  rgb[1] = a.g + (b.g - a.g) * u;
  rgb[2] = a.b + (b.b - a.b) * u;
}

template <std::size_t N>
void eval(const Stop (&stops)[N], float t, float rgb[kColorChannels])
{
  eval_stops(stops, N, t, rgb);
}

} // namespace

const char* colormap_name(ColorMap c)
{
  switch(c)
  {
    case ColorMap::Inferno: return "Inferno";
    case ColorMap::Viridis: return "Viridis";
    case ColorMap::Heat: return "Heat";
    case ColorMap::Ice: return "Ice";
    case ColorMap::Alert: return "Red alert";
    case ColorMap::Gray: return "Grayscale";
    case ColorMap::COUNT: break;
  }
  return "?";
}

const char* colormap_key(ColorMap c)
{
  switch(c)
  {
    case ColorMap::Inferno: return "inferno";
    case ColorMap::Viridis: return "viridis";
    case ColorMap::Heat: return "heat";
    case ColorMap::Ice: return "ice";
    case ColorMap::Alert: return "alert";
    case ColorMap::Gray: return "gray";
    case ColorMap::COUNT: break;
  }
  return "";
}

bool colormap_from_key(const std::string& key, ColorMap& out)
{
  for(int i = 0; i < kColorMapCount; ++i)
  {
    const ColorMap c = static_cast<ColorMap>(i);
    if(key == colormap_key(c))
    {
      out = c;
      return true;
    }
  }
  return false;
}

void colormap_eval(ColorMap c, float t, float rgb[kColorChannels])
{
  CC_REQUIRE(rgb != nullptr, return);
  switch(c)
  {
    case ColorMap::Inferno: eval(kInferno, t, rgb); break;
    case ColorMap::Viridis: eval(kViridis, t, rgb); break;
    case ColorMap::Heat: eval(kHeat, t, rgb); break;
    case ColorMap::Ice: eval(kIce, t, rgb); break;
    case ColorMap::Alert: eval(kAlert, t, rgb); break;
    case ColorMap::Gray:
    case ColorMap::COUNT: eval(kGray, t, rgb); break;
  }
}

std::vector<uint8_t> colormap_table(ColorMap c)
{
  std::vector<uint8_t> out(static_cast<std::size_t>(kColorMapTableSize) * kColorChannels);
  for(int i = 0; i < kColorMapTableSize; ++i)
  {
    float rgb[kColorChannels] = {0.0f, 0.0f, 0.0f};
    colormap_eval(c, static_cast<float>(i) / static_cast<float>(kColorMapTableSize - 1), rgb);
    for(int k = 0; k < kColorChannels; ++k)
    {
      const long level = std::lround(std::clamp(rgb[k], 0.0f, 1.0f) * kMaxLevel);
      out[static_cast<std::size_t>(i) * kColorChannels + static_cast<std::size_t>(k)] = static_cast<uint8_t>(level);
    }
  }
  return out;
}

} // namespace cc
