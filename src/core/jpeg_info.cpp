/**
 * @file jpeg_info.cpp
 * @brief JPEG marker-segment parser and IJG quality estimator.
 *
 * The constants here are format definitions from ITU-T T.81 and libjpeg,
 * not settings, so they stay in the source.
 */
#include "core/jpeg_info.h"

#include "util/contract.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cc {

namespace {

/** @brief Coefficients in one 8x8 quantisation table. */
constexpr int kTableSize = 64;
/** @brief Quantisation table slots a JPEG may define (Tq = 0..3). */
constexpr int kTableSlots = 4;
/** @brief Highest IJG quality. */
constexpr int kMaxQuality = 100;
/** @brief Quality below which IJG uses the 5000/q scaling branch. */
constexpr int kQualityKnee = 50;
/** @brief Numerator of the low-quality scaling branch. */
constexpr int kLowQualityScale = 5000;
/** @brief `200 - 2q` scaling branch constant. */
constexpr int kHighQualityScale = 200;
/** @brief Largest baseline quantiser value. */
constexpr int kMaxQuantValue = 255;
/** @brief Marker segments examined before the parser gives up (formal loop bound). */
constexpr std::size_t kMaxSegments = 65536;
/** @brief Bytes of a marker plus its length field. */
constexpr std::size_t kMarkerHeaderBytes = 4;
/** @brief Bytes of a frame header before the per-component entries. */
constexpr std::size_t kSofFixedBytes = 6;
/** @brief Bytes per component entry in a frame header. */
constexpr std::size_t kSofComponentBytes = 3;
/** @brief Offset of the component count in a frame header. */
constexpr std::size_t kSofComponentsOffset = 5;
/** @brief Length of the "ICC_PROFILE" tag and the shortest APP2 segment that can carry it. */
constexpr std::size_t kIccTagLength = 11;
constexpr std::size_t kIccMinLength = 12;
/** @brief Fit error that any real table beats (initial best). */
constexpr double kWorstFit = 1.0e30;

/* ---- marker codes (ITU-T T.81 Table B.1) -------------------------------- */
constexpr uint8_t kMarkerPrefix = 0xFF;
constexpr uint8_t kSOI = 0xD8;
constexpr uint8_t kEOI = 0xD9;
constexpr uint8_t kSOS = 0xDA;
constexpr uint8_t kDQT = 0xDB;
constexpr uint8_t kTEM = 0x01;
constexpr uint8_t kRST0 = 0xD0;
constexpr uint8_t kRST7 = 0xD7;
constexpr uint8_t kAPP0 = 0xE0;
constexpr uint8_t kAPP1 = 0xE1;
constexpr uint8_t kAPP2 = 0xE2;
constexpr uint8_t kAPP14 = 0xEE;
constexpr uint8_t kSOF0 = 0xC0;
constexpr uint8_t kSOF15 = 0xCF;
constexpr uint8_t kDHT = 0xC4;
constexpr uint8_t kJPG = 0xC8;
constexpr uint8_t kDAC = 0xCC;
/** @brief Progressive frame types: SOF2, SOF6, SOF10, SOF14. */
constexpr uint8_t kProgressiveSof[] = {0xC2, 0xC6, 0xCA, 0xCE};

/** @brief ITU-T T.81 Annex K sample luma table, natural (row-major) order. */
const int kStdLuma[kTableSize] = {16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,
                                  14, 13, 16, 24, 40,  57,  69,  56,  14, 17, 22, 29, 51,  87,  80,  62,
                                  18, 22, 37, 56, 68,  109, 103, 77,  24, 35, 55, 64, 81,  104, 113, 92,
                                  49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};
/** @brief ITU-T T.81 Annex K sample chroma table. */
const int kStdChroma[kTableSize] = {17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99,
                                    99, 99, 47, 66, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
                                    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};
/** @brief Zig-zag to natural index (`jpeg_natural_order` in libjpeg). */
const int kNatural[kTableSize] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
                                  41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
                                  30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

/** @brief Quantisation tables collected while parsing. */
struct Tables
{
  int values[kTableSlots][kTableSize] = {};
  bool present[kTableSlots] = {};
  int luma_slot = 0;
  int chroma_slot = 1;
};

/** @brief True for SOF0..SOF15 except the DHT, JPG and DAC codes that share the range. */
bool is_sof(uint8_t m) { return m >= kSOF0 && m <= kSOF15 && m != kDHT && m != kJPG && m != kDAC; }

/** @brief True for markers that carry no length field. */
bool is_standalone(uint8_t m) { return m == kSOI || m == kTEM || (m >= kRST0 && m <= kRST7); }

/** @brief Big-endian 16-bit read. */
int read_u16(const uint8_t* p) { return (static_cast<int>(p[0]) << 8) | p[1]; }

/**
 * @brief IJG `jpeg_quality_scaling` + `jpeg_add_quant_table` with force_baseline.
 * @param base Annex K table.
 * @param quality 1..100.
 * @param out Receives the scaled table.
 */
void ijg_table(const int* base, int quality, int* out)
{
  const int q = std::clamp(quality, 1, kMaxQuality);
  const int scale = q < kQualityKnee ? kLowQualityScale / q : kHighQualityScale - q * 2;
  for(int i = 0; i < kTableSize; ++i)
  {
    const int v = (base[i] * scale + kQualityKnee) / kMaxQuality;
    out[i] = std::clamp(v, 1, kMaxQuantValue);
  }
}

/**
 * @brief Brute-forces the quality whose IJG table is closest to the observed one.
 * @param observed The table from the file.
 * @param base Annex K luma or chroma table.
 * @param fit_error Receives the mean absolute deviation in percent of the mean coefficient.
 * @return The best quality in 1..100.
 */
int estimate_quality(const int* observed, const int* base, float& fit_error)
{
  int best_q = kJpegQualityUnknown;
  double best_err = kWorstFit;
  for(int q = 1; q <= kMaxQuality; ++q)
  {
    int predicted[kTableSize];
    ijg_table(base, q, predicted);
    double err = 0;
    for(int i = 0; i < kTableSize; ++i)
    {
      err += std::abs(predicted[i] - observed[i]);
    }
    if(err < best_err)
    {
      best_err = err;
      best_q = q;
    }
  }
  double mean_observed = 0;
  for(int i = 0; i < kTableSize; ++i)
  {
    mean_observed += observed[i];
  }
  mean_observed /= kTableSize;
  const double pct = 100.0;
  fit_error = mean_observed > 0 ? static_cast<float>(pct * (best_err / kTableSize) / mean_observed) : 0.0f;
  return best_q;
}

/** @brief Parses one DQT segment (may hold several tables). */
void parse_dqt(const uint8_t* seg, std::size_t seglen, Tables& tables, JpegInfo& info)
{
  std::size_t p = 0;
  for(int table = 0; table < kTableSlots && p < seglen; ++table)
  {
    const int precision = seg[p] >> 4;
    const int slot = seg[p] & 0x0F;
    ++p;
    if(slot >= kTableSlots)
    {
      return;
    }
    const std::size_t need = precision != 0 ? 2 * kTableSize : kTableSize;
    if(p + need > seglen)
    {
      return;
    }
    for(int k = 0; k < kTableSize; ++k)
    {
      const std::size_t at = p + (precision != 0 ? 2 * static_cast<std::size_t>(k) : static_cast<std::size_t>(k));
      tables.values[slot][kNatural[k]] = precision != 0 ? read_u16(seg + at) : seg[at];
    }
    tables.present[slot] = true;
    info.quant_tables++;
    p += need;
  }
}

/** @brief Names the chroma subsampling from luma and chroma sampling factors. */
std::string subsampling_name(int components, int h0, int v0, int h1, int v1)
{
  if(components == 1)
  {
    return "gray";
  }
  if(h0 == h1 && v0 == v1)
  {
    return "4:4:4";
  }
  if(h0 == 2 * h1 && v0 == v1)
  {
    return "4:2:2";
  }
  if(h0 == 2 * h1 && v0 == 2 * v1)
  {
    return "4:2:0";
  }
  if(h0 == h1 && v0 == 2 * v1)
  {
    return "4:4:0";
  }
  if(h0 == 4 * h1 && v0 == v1)
  {
    return "4:1:1";
  }
  return "?";
}

/** @brief Parses a frame header (SOFn). */
void parse_sof(uint8_t marker, const uint8_t* seg, std::size_t seglen, Tables& tables, JpegInfo& info)
{
  if(seglen < kSofFixedBytes)
  {
    return;
  }
  info.ok = true;
  info.progressive = std::find(std::begin(kProgressiveSof), std::end(kProgressiveSof), marker) != std::end(kProgressiveSof);
  info.height = read_u16(seg + 1);
  info.width = read_u16(seg + 3);
  info.components = seg[kSofComponentsOffset];
  int h0 = 1;
  int v0 = 1;
  int h1 = 1;
  int v1 = 1;
  for(int c = 0; c < info.components && c < kTableSlots; ++c)
  {
    const std::size_t at = kSofFixedBytes + static_cast<std::size_t>(c) * kSofComponentBytes;
    if(at + 2 >= seglen)
    {
      break;
    }
    const int hv = seg[at + 1];
    const int h = std::max(1, hv >> 4);
    const int v = std::max(1, hv & 0x0F);
    const int slot = seg[at + 2] & (kTableSlots - 1);
    if(c == 0)
    {
      h0 = h;
      v0 = v;
      tables.luma_slot = slot;
    }
    else if(c == 1)
    {
      h1 = h;
      v1 = v;
      tables.chroma_slot = slot;
    }
  }
  info.subsampling = subsampling_name(info.components, h0, v0, h1, v1);
}

/** @brief Records the application markers that identify metadata blocks. */
void parse_app(uint8_t marker, const uint8_t* seg, std::size_t seglen, JpegInfo& info)
{
  const std::size_t tag = 4;
  if(marker == kAPP0 && seglen > tag && std::equal(seg, seg + tag, "JFIF"))
  {
    info.has_jfif = true;
  }
  else if(marker == kAPP1 && seglen > tag && std::equal(seg, seg + tag, "Exif"))
  {
    info.has_exif = true;
  }
  else if(marker == kAPP2 && seglen >= kIccMinLength && std::equal(seg, seg + kIccTagLength, "ICC_PROFILE"))
  {
    info.has_icc = true;
  }
  else if(marker == kAPP14 && seglen > tag && std::equal(seg, seg + tag, "Adob"))
  {
    info.has_adobe = true;
  }
}

/**
 * @brief Skips entropy-coded data after an SOS segment.
 * @return Offset of the next marker's 0xFF, or n.
 */
std::size_t skip_scan(const uint8_t* d, std::size_t n, std::size_t from)
{
  std::size_t p = from;
  while(p + 1 < n)
  {
    const bool marker = d[p] == kMarkerPrefix && d[p + 1] != 0 && !(d[p + 1] >= kRST0 && d[p + 1] <= kRST7);
    if(marker)
    {
      break;
    }
    ++p;
  }
  return p;
}

/** @brief Dispatches one marker segment (everything except SOS) to its parser. */
void parse_segment(uint8_t marker, const uint8_t* seg, std::size_t seglen, Tables& tables, JpegInfo& info)
{
  if(marker == kDQT)
  {
    parse_dqt(seg, seglen, tables, info);
  }
  else if(is_sof(marker))
  {
    parse_sof(marker, seg, seglen, tables, info);
  }
  else
  {
    parse_app(marker, seg, seglen, info);
  }
}

/** @brief Turns the collected tables into quality estimates. */
void estimate_qualities(const Tables& tables, JpegInfo& info)
{
  if(tables.present[tables.luma_slot])
  {
    float e = 0;
    info.quality_luma = estimate_quality(tables.values[tables.luma_slot], kStdLuma, e);
    info.table_fit_error = e;
  }
  if(info.components > 1 && tables.present[tables.chroma_slot] && tables.chroma_slot != tables.luma_slot)
  {
    float e = 0;
    info.quality_chroma = estimate_quality(tables.values[tables.chroma_slot], kStdChroma, e);
    info.table_fit_error = std::max(info.table_fit_error, e);
  }
  if(info.quality_luma > 0 && info.quality_chroma > 0)
  {
    info.quality = static_cast<int>(std::lround(0.5 * (info.quality_luma + info.quality_chroma)));
  }
  else
  {
    info.quality = std::max(info.quality_luma, info.quality_chroma);
  }
}

} // namespace

std::string JpegInfo::summary(float custom_table_threshold_pct) const
{
  if(!ok)
  {
    return "not a JPEG";
  }
  char buf[256];
  std::snprintf(buf, sizeof buf, "%s JPEG, %s, q~%d%s", progressive ? "progressive" : "baseline", subsampling.c_str(), quality,
                table_fit_error > custom_table_threshold_pct ? " (custom tables)" : "");
  return buf;
}

JpegInfo parse_jpeg_info(const uint8_t* d, std::size_t n)
{
  JpegInfo info;
  if(d == nullptr || n < kMarkerHeaderBytes || d[0] != kMarkerPrefix || d[1] != kSOI)
  {
    return info;
  }
  Tables tables;
  std::size_t i = 2;
  for(std::size_t segments = 0; segments < kMaxSegments && i + kMarkerHeaderBytes <= n; ++segments)
  {
    if(d[i] != kMarkerPrefix || d[i + 1] == kMarkerPrefix)
    {
      ++i; /* stray byte or fill byte */
      continue;
    }
    const uint8_t m = d[i + 1];
    if(is_standalone(m))
    {
      i += 2;
      continue;
    }
    if(m == kEOI)
    {
      break;
    }
    const std::size_t len = static_cast<std::size_t>(read_u16(d + i + 2));
    if(len < 2 || i + 2 + len > n)
    {
      break;
    }
    if(m == kSOS)
    {
      info.scans++;
      i = skip_scan(d, n, i + 2 + len);
      continue;
    }
    parse_segment(m, d + i + kMarkerHeaderBytes, len - 2, tables, info);
    i += 2 + len;
  }
  if(!info.ok)
  {
    return info;
  }
  estimate_qualities(tables, info);
  if(info.subsampling.empty())
  {
    info.subsampling = "?";
  }
  return info;
}

} // namespace cc
