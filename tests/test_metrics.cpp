/**
 * @file test_metrics.cpp
 * @brief Colour maths against published CIEDE2000 pairs, heat-map kernels
 * and statistics on synthetic images.
 */
#include "core/config.h"
#include "core/metrics.h"
#include "test_util.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace cc;

namespace {

/** @brief A synthetic gradient image with a known pattern. */
Image make_gradient(int w, int h)
{
  Image a = Image::blank(w, h, kOpaqueAlpha);
  for(int y = 0; y < h; ++y)
  {
    for(int x = 0; x < w; ++x)
    {
      uint8_t* p = a.px(x, y);
      p[0] = static_cast<uint8_t>(x * 4);
      p[1] = static_cast<uint8_t>(y * 5);
      p[2] = static_cast<uint8_t>((x + y) * 2);
      p[3] = kOpaqueAlpha;
    }
  }
  return a;
}

/** @brief Sharma, Wu & Dalal (2005) test pairs. */
void check_delta_e2000()
{
  struct Pair
  {
    Lab a;
    Lab b;
    float de;
  };
  const Pair pairs[] = {
    {{50.0000f, 2.6772f, -79.7751f}, {50.0000f, 0.0000f, -82.7485f}, 2.0425f},   /* #1 */
    {{50.0000f, 0.0000f, 0.0000f}, {50.0000f, -1.0000f, 2.0000f}, 2.3669f},      /* #7 */
    {{50.0000f, 2.4900f, -0.0010f}, {50.0000f, -2.4900f, 0.0009f}, 7.1792f},     /* #9 */
    {{50.0000f, -0.0010f, 2.4900f}, {50.0000f, 0.0009f, -2.4900f}, 4.8045f},     /* #13 */
    {{50.0000f, 2.5000f, 0.0000f}, {73.0000f, 25.0000f, -18.0000f}, 27.1492f},   /* #17 */
    {{60.2574f, -34.0099f, 36.2677f}, {60.4626f, -34.1751f, 39.4387f}, 1.2644f}, /* #25 */
    {{2.0776f, 0.0795f, -1.1350f}, {0.9033f, -0.0636f, -0.5514f}, 0.9082f},      /* #34 */
  };
  for(const Pair& p : pairs)
  {
    CHECK_NEAR(delta_e2000(p.a, p.b), p.de, 2e-3);
    CHECK_NEAR(delta_e2000(p.b, p.a), p.de, 2e-3); /* symmetric */
  }
  CHECK_NEAR(delta_e76(Lab{50, 0, 0}, Lab{50, 3, 4}), 5.0, 1e-5);
}

/** @brief sRGB to Lab sanity. */
void check_lab()
{
  const Lab white = srgb8_to_lab(255, 255, 255);
  CHECK_NEAR(white.L, 100.0, 0.05);
  CHECK_NEAR(white.a, 0.0, 0.05);
  CHECK_NEAR(white.b, 0.0, 0.05);
  const Lab black = srgb8_to_lab(0, 0, 0);
  CHECK_NEAR(black.L, 0.0, 1e-4);
  const Lab mid = srgb8_to_lab(119, 119, 119); /* ~18 % grey -> L ~ 50 */
  CHECK_NEAR(mid.L, 50.0, 1.0);
  const Lab red = srgb8_to_lab(255, 0, 0);
  CHECK(red.a > 60.0f && red.b > 40.0f);
}

/** @brief Heat maps and statistics of identical and perturbed images. */
void check_heatmaps(const MetricsConfig& mc, const ThreadingConfig& tc)
{
  const int W = 64;
  const int H = 48;
  const Image a = make_gradient(W, H);
  Image b = a;
  for(int m = 0; m < kMetricCount; ++m)
  {
    const HeatMap hm = compute_heatmap(a, b, static_cast<Metric>(m), mc, tc);
    CHECK(hm.valid());
    CHECK_NEAR(hm.max, 0.0, 1e-6);
    CHECK_NEAR(hm.mean, 0.0, 1e-6);
  }
  const PairStats st = compute_stats(a, b, mc, tc);
  CHECK(st.ok);
  CHECK_NEAR(st.mse, 0.0, 1e-9);
  CHECK_NEAR(st.psnr, mc.psnr_identical_db, 1e-9);
  CHECK_NEAR(st.ssim, 1.0, 1e-4);
  CHECK_NEAR(st.pct_identical, 100.0, 1e-9);

  /* perturb one pixel by +10 in red */
  b.px(10, 10)[0] = static_cast<uint8_t>(std::min(255, b.px(10, 10)[0] + 10));
  const HeatMap abs = compute_heatmap(a, b, Metric::AbsDiff, mc, tc);
  CHECK_NEAR(abs.at(10, 10), 10.0, 1e-6);
  CHECK_NEAR(abs.at(11, 10), 0.0, 1e-6);
  CHECK_NEAR(abs.max, 10.0, 1e-6);
  const HeatMap sq = compute_heatmap(a, b, Metric::SquaredError, mc, tc);
  CHECK_NEAR(sq.at(10, 10), 100.0 / 3.0, 1e-4);
  const HeatMap lum = compute_heatmap(a, b, Metric::LumaDiff, mc, tc);
  CHECK_NEAR(lum.at(10, 10), 2.99, 1e-3);
  const HeatMap ssim = compute_heatmap(a, b, Metric::SSIM, mc, tc);
  CHECK(ssim.at(10, 10) > 0.0f);
  CHECK_NEAR(ssim.at(40, 40), 0.0, 1e-6); /* far away from the change */
  const PairStats st2 = compute_stats(a, b, mc, tc);
  CHECK_NEAR(st2.mse, (100.0 / 3.0) / (W * H), 1e-9);
  CHECK(st2.psnr > 40.0 && st2.psnr < mc.psnr_identical_db);
  CHECK(st2.ssim < 1.0 && st2.ssim > 0.99);

  /* block average: 8x8 cells become constant */
  HeatMap blk = compute_heatmap(a, b, Metric::AbsDiff, mc, tc);
  block_average(blk, mc.block_size_px, mc);
  CHECK(blk.block_averaged);
  CHECK_NEAR(blk.at(8, 8), 10.0 / 64.0, 1e-6);
  CHECK_NEAR(blk.at(15, 15), 10.0 / 64.0, 1e-6);
  CHECK_NEAR(blk.at(16, 8), 0.0, 1e-9);

  /* percentiles / histogram are populated */
  CHECK(abs.hist.size() == static_cast<std::size_t>(mc.histogram_bins));
  uint64_t total = 0;
  for(const uint32_t h : abs.hist)
  {
    total += h;
  }
  CHECK(total == static_cast<uint64_t>(W) * H);
  CHECK(abs.p99 <= abs.max + 1e-6f);
  CHECK(abs.p_auto <= abs.max + 1e-6f);
  CHECK(mc.auto_scale_percentile != 99 || std::fabs(abs.p_auto - abs.p99) < 1e-6f);
}

/** @brief Whole-image shift: mean abs = 7 everywhere, PSNR = 20 log10(255/7). */
void check_shift(const MetricsConfig& mc, const ThreadingConfig& tc)
{
  Image a2 = make_gradient(64, 48);
  for(std::size_t i = 0; i < a2.rgba.size(); i += kImageChannels)
  {
    for(std::size_t k = 0; k < 3; ++k)
    {
      a2.rgba[i + k] = static_cast<uint8_t>(std::max(7, static_cast<int>(a2.rgba[i + k]))); /* avoid clipping at 0 */
    }
  }
  Image c = a2;
  for(std::size_t i = 0; i < c.rgba.size(); i += kImageChannels)
  {
    for(std::size_t k = 0; k < 3; ++k)
    {
      c.rgba[i + k] = static_cast<uint8_t>(a2.rgba[i + k] - 7);
    }
  }
  const PairStats st3 = compute_stats(a2, c, mc, tc);
  CHECK_NEAR(st3.mse, 49.0, 1e-9);
  CHECK_NEAR(st3.psnr, 20.0 * std::log10(255.0 / 7.0), 1e-6);
  CHECK_NEAR(st3.mean_abs, 7.0, 1e-9);
}

/** @brief The separable Gaussian keeps a constant image constant and sums to one. */
void check_gaussian(const MetricsConfig& mc, const ThreadingConfig& tc)
{
  const int W = 64;
  const int H = 48;
  const std::size_t n = static_cast<std::size_t>(W) * H;
  const std::vector<float> flat(n, 3.5f);
  std::vector<float> out;
  gaussian_blur_separable(flat, out, W, H, mc.ssim.window, mc.ssim.sigma, tc);
  CHECK_NEAR(out[0], 3.5, 1e-5);
  CHECK_NEAR(out[static_cast<std::size_t>(H / 2) * W + W / 2], 3.5, 1e-5);
  std::vector<float> impulse(n, 0.0f);
  impulse[static_cast<std::size_t>(H / 2) * W + W / 2] = 1.0f;
  gaussian_blur_separable(impulse, out, W, H, mc.ssim.window, mc.ssim.sigma, tc);
  double sum = 0;
  for(const float v : out)
  {
    sum += static_cast<double>(v);
  }
  CHECK_NEAR(sum, 1.0, 1e-4);
}

} // namespace

int main()
{
  const Config& cfg = default_config();
  CHECK(cfg.schema_version == kConfigSchemaVersion);
  check_delta_e2000();
  check_lab();
  check_heatmaps(cfg.metrics, cfg.threading);
  check_shift(cfg.metrics, cfg.threading);
  check_gaussian(cfg.metrics, cfg.threading);
  return test_summary("test_metrics");
}
