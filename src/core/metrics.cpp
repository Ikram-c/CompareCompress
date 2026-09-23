/**
 * @file metrics.cpp
 * @brief Colour maths, heat-map kernels and statistics.
 *
 * Constants in this file are physical or standards constants (IEC 61966-2-1
 * sRGB curve, the D65 sRGB to XYZ matrix, CIE Lab thresholds, CIEDE2000
 * weights, Rec.601 luma coefficients).  They define the metrics and are not
 * settings, so they live here rather than in config.yaml.
 */
#include "core/metrics.h"

#include "core/accel.h"
#include "core/cv_ops.h"
#include "util/contract.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <numeric>

namespace cc {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegreesPerTurn = 360.0f;
constexpr float kHalfTurnDegrees = 180.0f;

/* ---- sRGB (IEC 61966-2-1) ---------------------------------------------- */
constexpr int kLevels = 256;
constexpr float kMaxLevel = 255.0f;
constexpr float kSrgbLinearCutoff = 0.04045f;
constexpr float kSrgbLinearSlope = 12.92f;
constexpr float kSrgbOffset = 0.055f;
constexpr float kSrgbScale = 1.055f;
constexpr float kSrgbGamma = 2.4f;

/* ---- sRGB (D65) to XYZ, Lindbloom / IEC matrix -------------------------- */
constexpr float kXr = 0.4124564f, kXg = 0.3575761f, kXb = 0.1804375f;
constexpr float kYr = 0.2126729f, kYg = 0.7151522f, kYb = 0.0721750f;
constexpr float kZr = 0.0193339f, kZg = 0.1191920f, kZb = 0.9503041f;
/* D65 reference white */
constexpr float kXn = 0.95047f;
constexpr float kYn = 1.0f;
constexpr float kZn = 1.08883f;

/* ---- CIE Lab ----------------------------------------------------------- */
constexpr float kLabEpsilon = 216.0f / 24389.0f;
constexpr float kLabKappa = 24389.0f / 27.0f;
constexpr float kLabLScale = 116.0f;
constexpr float kLabLOffset = 16.0f;
constexpr float kLabAScale = 500.0f;
constexpr float kLabBScale = 200.0f;

/* ---- CIEDE2000 (Sharma, Wu & Dalal 2005) ------------------------------- */
constexpr float kPow25To7 = 6103515625.0f; /* 25^7 */
constexpr float kT0 = 0.17f, kT0Deg = 30.0f;
constexpr float kT1 = 0.24f;
constexpr float kT2 = 0.32f, kT2Deg = 6.0f;
constexpr float kT3 = 0.20f, kT3Deg = 63.0f;
constexpr float kThetaScale = 30.0f, kThetaCentre = 275.0f, kThetaWidth = 25.0f;
constexpr float kSlWeight = 0.015f, kSlCentre = 50.0f, kSlOffset = 20.0f;
constexpr float kScWeight = 0.045f;
constexpr float kShWeight = 0.015f;

/* ---- Rec.601 luma ------------------------------------------------------ */
constexpr float kLumaR = 0.299f;
constexpr float kLumaG = 0.587f;
constexpr float kLumaB = 0.114f;

/** @brief Progress reported when a heat map is being copied back from the device. */
constexpr float kHeatDownloadProgress = 0.9f;
/** @brief Progress reported after the first / second pair of blurs in the SSIM map. */
constexpr float kSsimProgressA = 0.3f;
constexpr float kSsimProgressB = 0.6f;
/** @brief Progress reported after the per-pixel pass of compute_stats(). */
constexpr float kStatsProgressHalf = 0.5f;
/** @brief Percentile denominators. */
constexpr std::size_t kPercent = 100;
constexpr std::size_t kP50 = 50, kP95 = 95, kP99 = 99;
/** @brief PSNR = 10 log10(peak^2 / MSE). */
constexpr double kDecibelFactor = 10.0;

/** @brief sRGB to linear lookup for the 256 8-bit levels. */
const std::array<float, kLevels>& srgb_to_linear_lut()
{
  static const std::array<float, kLevels> lut = []() {
    std::array<float, kLevels> t{};
    for(int i = 0; i < kLevels; ++i)
    {
      const float c = static_cast<float>(i) / kMaxLevel;
      t[static_cast<std::size_t>(i)] = c <= kSrgbLinearCutoff ? c / kSrgbLinearSlope : std::pow((c + kSrgbOffset) / kSrgbScale, kSrgbGamma);
    }
    return t;
  }();
  return lut;
}

/** @brief The CIE Lab companding function f(t). */
inline float lab_f(float t) { return t > kLabEpsilon ? std::cbrt(t) : (kLabKappa * t + kLabLOffset) / kLabLScale; }

inline float deg2rad(float d) { return d * (kPi / kHalfTurnDegrees); }
inline float rad2deg(float r) { return r * (kHalfTurnDegrees / kPi); }

/** @brief Hue angle in degrees [0, 360) of (a, b); 0 for the achromatic axis. */
float hue_degrees(float a, float b)
{
  if(a == 0.0f && b == 0.0f)
  {
    return 0.0f;
  }
  float h = rad2deg(std::atan2(b, a));
  if(h < 0.0f)
  {
    h += kDegreesPerTurn;
  }
  return h;
}

/** @brief Hue difference h2' - h1' folded into (-180, 180]. */
float hue_difference(float h1, float h2, float chroma_product)
{
  if(chroma_product == 0.0f)
  {
    return 0.0f;
  }
  const float d = h2 - h1;
  if(std::fabs(d) <= kHalfTurnDegrees)
  {
    return d;
  }
  return d > kHalfTurnDegrees ? d - kDegreesPerTurn : d + kDegreesPerTurn;
}

/** @brief Mean hue of h1' and h2' on the circle. */
float hue_mean(float h1, float h2, float chroma_product)
{
  if(chroma_product == 0.0f)
  {
    return h1 + h2;
  }
  if(std::fabs(h1 - h2) <= kHalfTurnDegrees)
  {
    return 0.5f * (h1 + h2);
  }
  return h1 + h2 < kDegreesPerTurn ? 0.5f * (h1 + h2 + kDegreesPerTurn) : 0.5f * (h1 + h2 - kDegreesPerTurn);
}

} // namespace

/* ---- colour ------------------------------------------------------------ */

const float* srgb_to_linear_table() { return srgb_to_linear_lut().data(); }

Lab srgb8_to_lab(uint8_t r8, uint8_t g8, uint8_t b8)
{
  const std::array<float, kLevels>& lut = srgb_to_linear_lut();
  const float r = lut[r8];
  const float g = lut[g8];
  const float b = lut[b8];
  const float X = kXr * r + kXg * g + kXb * b;
  const float Y = kYr * r + kYg * g + kYb * b;
  const float Z = kZr * r + kZg * g + kZb * b;
  const float fx = lab_f(X / kXn);
  const float fy = lab_f(Y / kYn);
  const float fz = lab_f(Z / kZn);
  return Lab{kLabLScale * fy - kLabLOffset, kLabAScale * (fx - fy), kLabBScale * (fy - fz)};
}

float luma601(uint8_t r, uint8_t g, uint8_t b) { return kLumaR * r + kLumaG * g + kLumaB * b; }

float delta_e76(const Lab& x, const Lab& y)
{
  const float dL = x.L - y.L;
  const float da = x.a - y.a;
  const float db = x.b - y.b;
  return std::sqrt(dL * dL + da * da + db * db);
}

float delta_e2000(const Lab& x, const Lab& y)
{
  const float C1 = std::sqrt(x.a * x.a + x.b * x.b);
  const float C2 = std::sqrt(y.a * y.a + y.b * y.b);
  const float Cbar7 = std::pow(0.5f * (C1 + C2), 7.0f);
  const float G = 0.5f * (1.0f - std::sqrt(Cbar7 / (Cbar7 + kPow25To7)));
  const float a1p = (1.0f + G) * x.a;
  const float a2p = (1.0f + G) * y.a;
  const float C1p = std::sqrt(a1p * a1p + x.b * x.b);
  const float C2p = std::sqrt(a2p * a2p + y.b * y.b);
  const float chroma_product = C1p * C2p;
  const float h1p = hue_degrees(a1p, x.b);
  const float h2p = hue_degrees(a2p, y.b);
  const float dLp = y.L - x.L;
  const float dCp = C2p - C1p;
  const float dhp = hue_difference(h1p, h2p, chroma_product);
  const float dHp = 2.0f * std::sqrt(chroma_product) * std::sin(deg2rad(dhp * 0.5f));
  const float Lbp = 0.5f * (x.L + y.L);
  const float Cbp = 0.5f * (C1p + C2p);
  const float hbp = hue_mean(h1p, h2p, chroma_product);
  const float T = 1.0f - kT0 * std::cos(deg2rad(hbp - kT0Deg)) + kT1 * std::cos(deg2rad(2.0f * hbp))
                  + kT2 * std::cos(deg2rad(3.0f * hbp + kT2Deg)) - kT3 * std::cos(deg2rad(4.0f * hbp - kT3Deg));
  const float theta_arg = (hbp - kThetaCentre) / kThetaWidth;
  const float dtheta = kThetaScale * std::exp(-theta_arg * theta_arg);
  const float Cbp7 = std::pow(Cbp, 7.0f);
  const float RC = 2.0f * std::sqrt(Cbp7 / (Cbp7 + kPow25To7));
  const float Ldev = (Lbp - kSlCentre) * (Lbp - kSlCentre);
  const float SL = 1.0f + (kSlWeight * Ldev) / std::sqrt(kSlOffset + Ldev);
  const float SC = 1.0f + kScWeight * Cbp;
  const float SH = 1.0f + kShWeight * Cbp * T;
  const float RT = -std::sin(deg2rad(2.0f * dtheta)) * RC;
  const float tL = dLp / SL;
  const float tC = dCp / SC;
  const float tH = dHp / SH;
  return std::sqrt(std::max(0.0f, tL * tL + tC * tC + tH * tH + RT * tC * tH));
}

/* ---- HeatMap statistics ------------------------------------------------ */

namespace {

/** @brief Fine histogram over [0, max] used to estimate percentiles. */
std::vector<uint32_t> fine_histogram(const std::vector<float>& v, float max, int bins)
{
  std::vector<uint32_t> fh(static_cast<std::size_t>(bins), 0);
  const float scale = max > 0 ? static_cast<float>(bins - 1) / max : 0.0f;
  for(const float x : v)
  {
    const int b = std::clamp(static_cast<int>(x * scale), 0, bins - 1);
    fh[static_cast<std::size_t>(b)]++;
  }
  return fh;
}

/** @brief Value below which `percent` % of the samples fall, from the fine histogram. */
float percentile_from_histogram(const std::vector<uint32_t>& fh, std::size_t n, std::size_t percent, float bin_width)
{
  const std::size_t target = n * percent / kPercent;
  std::size_t acc = 0;
  for(std::size_t b = 0; b < fh.size(); ++b)
  {
    acc += fh[b];
    if(acc >= target)
    {
      return static_cast<float>(b) * bin_width;
    }
  }
  return 0.0f;
}

} // namespace

void HeatMap::finalize(const MetricsConfig& cfg)
{
  CC_REQUIRE(cfg.histogram_bins >= 2 && cfg.histogram_fine_bins >= cfg.histogram_bins, return);
  CC_REQUIRE(cfg.auto_scale_percentile >= 1 && cfg.auto_scale_percentile <= static_cast<int>(kPercent), return);
  hist.assign(static_cast<std::size_t>(cfg.histogram_bins), 0);
  if(!valid())
  {
    return;
  }
  min = v[0];
  max = v[0];
  double sum = 0;
  for(const float x : v)
  {
    min = std::min(min, x);
    max = std::max(max, x);
    sum += static_cast<double>(x);
  }
  mean = static_cast<float>(sum / static_cast<double>(v.size()));
  const int fine = cfg.histogram_fine_bins;
  const std::vector<uint32_t> fh = fine_histogram(v, max, fine);
  const float bin_w = max > 0 ? max / static_cast<float>(fine - 1) : 0.0f;
  p50 = percentile_from_histogram(fh, v.size(), kP50, bin_w);
  p95 = percentile_from_histogram(fh, v.size(), kP95, bin_w);
  p99 = percentile_from_histogram(fh, v.size(), kP99, bin_w);
  p_auto = percentile_from_histogram(fh, v.size(), static_cast<std::size_t>(cfg.auto_scale_percentile), bin_w);
  for(int b = 0; b < fine; ++b)
  {
    hist[static_cast<std::size_t>(b * cfg.histogram_bins / fine)] += fh[static_cast<std::size_t>(b)];
  }
}

/* ---- Gaussian filtering and the SSIM map (OpenCV, OpenCL when enabled) ------ */

void gaussian_blur_separable(const std::vector<float>& src, std::vector<float>& dst, int w, int h, int window, float sigma,
                             const ThreadingConfig& /*threading: OpenCV's pool is sized once in init_acceleration()*/)
{
  CC_REQUIRE(w > 0 && h > 0 && src.size() == static_cast<std::size_t>(w) * static_cast<std::size_t>(h), return);
  CC_REQUIRE(window >= 1 && (window % 2) == 1 && sigma > 0.0f, return);
  dst.assign(src.size(), 0.0f);
  const cv::Mat in(h, w, CV_32FC1, const_cast<float*>(src.data()));
  cv::Mat out(h, w, CV_32FC1, dst.data());
  /* BORDER_REPLICATE reads clamp to the edge, like CLK_ADDRESS_CLAMP_TO_EDGE in the CLIJ2 kernel. */
  cv::GaussianBlur(in, out, cv::Size(window, window), static_cast<double>(sigma), static_cast<double>(sigma), cv::BORDER_REPLICATE);
}

namespace {

/**
 * @brief SSIM map on luma: local statistics through a Gaussian window
 * (the CLIJ2 variance_box idea with a Gaussian instead of a box window).
 * @param a Original, CV_8UC4.
 * @param b Site copy, same size as `a`.
 * @param cfg SSIM window, sigma and stabilising constants.
 * @param out Receives 1 - SSIM per pixel (CV_32FC1).
 * @param progress Optional progress sink.
 */
void ssim_map(const cv::UMat& a, const cv::UMat& b, const MetricsConfig& cfg, cv::UMat& out, Progress* progress)
{
  cv::UMat ya;
  cv::UMat yb;
  cvops::luma_pair(a, b, ya, yb);
  cv::UMat aa;
  cv::UMat bb;
  cv::UMat ab;
  cv::multiply(ya, ya, aa);
  cv::multiply(yb, yb, bb);
  cv::multiply(ya, yb, ab);
  const cv::Size win(cfg.ssim.window, cfg.ssim.window);
  const double sigma = static_cast<double>(cfg.ssim.sigma);
  cv::UMat mu_a;
  cv::UMat mu_b;
  cv::UMat e_aa;
  cv::UMat e_bb;
  cv::UMat e_ab;
  cv::GaussianBlur(ya, mu_a, win, sigma, sigma, cv::BORDER_REPLICATE);
  cv::GaussianBlur(yb, mu_b, win, sigma, sigma, cv::BORDER_REPLICATE);
  if(progress != nullptr)
  {
    progress->fraction = kSsimProgressA;
  }
  cv::GaussianBlur(aa, e_aa, win, sigma, sigma, cv::BORDER_REPLICATE);
  cv::GaussianBlur(bb, e_bb, win, sigma, sigma, cv::BORDER_REPLICATE);
  cv::GaussianBlur(ab, e_ab, win, sigma, sigma, cv::BORDER_REPLICATE);
  if(progress != nullptr)
  {
    progress->fraction = kSsimProgressB;
  }
  const float C1 = (cfg.ssim.k1 * kMaxLevel) * (cfg.ssim.k1 * kMaxLevel);
  const float C2 = (cfg.ssim.k2 * kMaxLevel) * (cfg.ssim.k2 * kMaxLevel);
  cvops::ssim_combine(mu_a, mu_b, e_aa, e_bb, e_ab, C1, C2, out);
}

/** @brief True when both images are valid and the same size. */
bool same_size(const Image& a, const Image& b) { return a.valid() && b.valid() && a.w == b.w && a.h == b.h; }

/** @brief Pixel count of an image. */
std::size_t pixel_count(const Image& a) { return static_cast<std::size_t>(a.w) * static_cast<std::size_t>(a.h); }

} // namespace

/* ---- heat maps --------------------------------------------------------- */

HeatMap compute_heatmap(const Image& a, const Image& b, Metric m, const MetricsConfig& cfg,
                        const ThreadingConfig& /*threading: see gaussian_blur_separable()*/, Progress* progress)
{
  HeatMap hm;
  hm.metric = m;
  CC_REQUIRE(same_size(a, b), return hm);
  CC_REQUIRE(m != Metric::COUNT, return hm);
  hm.w = a.w;
  hm.h = a.h;
  use_gpu_for(pixel_count(a));
  const cv::UMat ua = cvops::upload(a);
  const cv::UMat ub = cvops::upload(b);
  cv::UMat map;
  if(m == Metric::SSIM)
  {
    ssim_map(ua, ub, cfg, map, progress);
  }
  else
  {
    cvops::metric_map(ua, ub, m, map);
  }
  if(progress != nullptr)
  {
    progress->fraction = kHeatDownloadProgress;
  }
  cvops::download(map, hm.v);
  hm.finalize(cfg);
  return hm;
}

void block_average(HeatMap& hm, int block, const MetricsConfig& cfg)
{
  if(!hm.valid() || block < 2)
  {
    return;
  }
  const int bw = (hm.w + block - 1) / block;
  const int bh = (hm.h + block - 1) / block;
  const std::size_t cells = static_cast<std::size_t>(bw) * static_cast<std::size_t>(bh);
  std::vector<float> means(cells, 0.0f);
  std::vector<int> counts(cells, 0);
  for(int y = 0; y < hm.h; ++y)
  {
    for(int x = 0; x < hm.w; ++x)
    {
      const std::size_t bi = static_cast<std::size_t>(y / block) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(x / block);
      means[bi] += hm.at(x, y);
      counts[bi]++;
    }
  }
  for(std::size_t i = 0; i < cells; ++i)
  {
    means[i] /= static_cast<float>(std::max(1, counts[i]));
  }
  for(int y = 0; y < hm.h; ++y)
  {
    for(int x = 0; x < hm.w; ++x)
    {
      const std::size_t bi = static_cast<std::size_t>(y / block) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(x / block);
      hm.v[static_cast<std::size_t>(y) * static_cast<std::size_t>(hm.w) + static_cast<std::size_t>(x)] = means[bi];
    }
  }
  hm.block_averaged = true;
  hm.finalize(cfg);
}

PairStats compute_stats(const Image& a, const Image& b, const MetricsConfig& cfg, const ThreadingConfig& /*threading*/,
                        Progress* progress)
{
  PairStats s;
  CC_REQUIRE(same_size(a, b), return s);
  const std::size_t count = pixel_count(a);
  const double n = static_cast<double>(count);
  use_gpu_for(count);
  const cv::UMat ua = cvops::upload(a);
  const cv::UMat ub = cvops::upload(b);
  cv::UMat abs_map;
  cv::UMat sq_map;
  cv::UMat de_map;
  cvops::pair_maps(ua, ub, abs_map, sq_map, de_map);
  s.mse = cv::mean(sq_map)[0];
  s.mean_abs = cv::mean(abs_map)[0];
  s.mean_de2000 = cv::mean(de_map)[0];
  double de_max = 0;
  cv::minMaxLoc(de_map, nullptr, &de_max);
  s.max_de2000 = de_max;
  cv::UMat over;
  cv::compare(de_map, cv::Scalar(static_cast<double>(cfg.jnd_delta_e2000)), over, cv::CMP_GT);
  const double over_jnd = static_cast<double>(cv::countNonZero(over));
  const double identical = n - static_cast<double>(cv::countNonZero(abs_map));
  if(progress != nullptr)
  {
    progress->fraction = kStatsProgressHalf;
  }
  const double percent = 100.0;
  s.psnr = s.mse > 0 ? kDecibelFactor * std::log10(static_cast<double>(kMaxLevel) * static_cast<double>(kMaxLevel) / s.mse)
                     : cfg.psnr_identical_db;
  s.pct_over_jnd = percent * over_jnd / n;
  s.pct_identical = percent * identical / n;
  cv::UMat sm;
  ssim_map(ua, ub, cfg, sm, nullptr);
  s.ssim = 1.0 - cv::mean(sm)[0];
  s.ok = true;
  if(progress != nullptr)
  {
    progress->fraction = 1.0f;
  }
  return s;
}

} // namespace cc
