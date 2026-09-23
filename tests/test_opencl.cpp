/**
 * @file test_opencl.cpp
 * @brief The OpenCL path must reproduce the CPU path: kernel self-test, every
 * heat-map metric, the pair statistics and the sRGB-aware resampler, on a
 * JPEG-compressed test pair.  Skipped (and passing) when the machine has no
 * OpenCL device; run it on the target Mac to check its driver.
 */
#include "core/accel.h"
#include "core/config.h"
#include "core/cv_ops.h"
#include "core/image.h"
#include "core/metrics.h"
#include "test_util.h"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace cc;

namespace {

constexpr int kW = 720;
constexpr int kH = 540;

/** @brief A photo-like test image: gradients, a colour wheel, fine texture and hard edges. */
Image make_scene(int w, int h)
{
  Image a = Image::blank(w, h, kOpaqueAlpha);
  for(int y = 0; y < h; ++y)
  {
    for(int x = 0; x < w; ++x)
    {
      uint8_t* p = a.px(x, y);
      const double fx = static_cast<double>(x) / w;
      const double fy = static_cast<double>(y) / h;
      const double ang = std::atan2(fy - 0.5, fx - 0.5);
      const int texture = ((x * 7 + y * 13) % 17) - 8;
      p[0] = static_cast<uint8_t>(std::clamp(static_cast<int>(255 * fx + 30 * std::sin(ang * 3)) + texture, 0, 255));
      p[1] = static_cast<uint8_t>(std::clamp(static_cast<int>(255 * fy + 30 * std::cos(ang * 2)) - texture, 0, 255));
      p[2] = static_cast<uint8_t>(((x / 40 + y / 40) % 2 == 0) ? 40 : 210);
    }
  }
  return a;
}

/** @brief What a site does: JPEG at quality 55 with 4:2:0 (OpenCV's encoder). */
Image jpeg_roundtrip(const Image& src)
{
  const cv::Mat rgba(src.h, src.w, CV_8UC4, const_cast<uint8_t*>(src.rgba.data()));
  cv::Mat bgr;
  cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
  std::vector<uint8_t> jpg;
  cv::imencode(".jpg", bgr, jpg, {cv::IMWRITE_JPEG_QUALITY, 55});
  Image out;
  const LoadResult r = decode_image(jpg.data(), jpg.size(), 65536, out);
  CHECK(r.ok);
  return out;
}

/** @brief Fraction of values differing by more than `tol`. */
double mismatch_fraction(const std::vector<float>& a, const std::vector<float>& b, float tol)
{
  if(a.size() != b.size() || a.empty())
  {
    return 1.0;
  }
  std::size_t bad = 0;
  for(std::size_t i = 0; i < a.size(); ++i)
  {
    if(!(std::fabs(a[i] - b[i]) <= tol))
    {
      ++bad;
    }
  }
  return static_cast<double>(bad) / static_cast<double>(a.size());
}

void check_heatmaps(const Image& a, const Image& b, const Config& cfg)
{
  /* per-metric tolerance in the metric's own unit */
  const float tol[kMetricCount] = {0.0f, 1e-3f, 1e-3f, 5e-3f, 2e-3f, 1e-2f};
  for(int m = 0; m < kMetricCount; ++m)
  {
    set_opencl_enabled(true);
    const unsigned long before = cvops::gpu_launches();
    const HeatMap g = compute_heatmap(a, b, static_cast<Metric>(m), cfg.metrics, cfg.threading);
    CHECK(cvops::gpu_launches() > before); /* really ran on the device */
    set_opencl_enabled(false);
    const unsigned long before_cpu = cvops::gpu_launches();
    const HeatMap c = compute_heatmap(a, b, static_cast<Metric>(m), cfg.metrics, cfg.threading);
    CHECK(cvops::gpu_launches() == before_cpu); /* and this one did not */
    CHECK(g.valid() && c.valid());
    const double bad = mismatch_fraction(g.v, c.v, tol[m]);
    std::printf("  %-18s GPU vs CPU: %.4f %% outside %.0e, mean %.5f / %.5f\n", metric_name(static_cast<Metric>(m)), bad * 100.0,
                static_cast<double>(tol[m]), static_cast<double>(g.mean), static_cast<double>(c.mean));
    CHECK(bad < 1e-4);
    CHECK_NEAR(g.mean, c.mean, 1e-3 * static_cast<double>(std::max(1.0f, c.mean)));
    CHECK_NEAR(g.p95, c.p95, 2e-2 * static_cast<double>(std::max(1.0f, c.p95)));
  }
}

void check_stats(const Image& a, const Image& b, const Config& cfg)
{
  set_opencl_enabled(true);
  const PairStats g = compute_stats(a, b, cfg.metrics, cfg.threading);
  set_opencl_enabled(false);
  const PairStats c = compute_stats(a, b, cfg.metrics, cfg.threading);
  CHECK(g.ok && c.ok);
  std::printf("  stats GPU: psnr %.4f ssim %.6f dE %.4f  | CPU: psnr %.4f ssim %.6f dE %.4f\n", g.psnr, g.ssim, g.mean_de2000, c.psnr,
              c.ssim, c.mean_de2000);
  CHECK_NEAR(g.mse, c.mse, 1e-4 * c.mse);
  CHECK_NEAR(g.psnr, c.psnr, 1e-3);
  CHECK_NEAR(g.ssim, c.ssim, 1e-4);
  CHECK_NEAR(g.mean_abs, c.mean_abs, 1e-4 * std::max(1.0, c.mean_abs));
  CHECK_NEAR(g.mean_de2000, c.mean_de2000, 1e-3);
  CHECK_NEAR(g.max_de2000, c.max_de2000, 1e-2);
  CHECK_NEAR(g.pct_over_jnd, c.pct_over_jnd, 0.05);
  CHECK_NEAR(g.pct_identical, c.pct_identical, 1e-9);
}

void check_resample(const Image& a)
{
  const int sizes[][2] = {{333, 250}, {1080, 810}, {720, 300}};
  for(const auto& sz : sizes)
  {
    set_opencl_enabled(true);
    const Image g = resample(a, sz[0], sz[1]);
    set_opencl_enabled(false);
    const Image c = resample(a, sz[0], sz[1]);
    CHECK(g.valid() && c.valid() && g.w == sz[0] && g.h == sz[1]);
    int worst = 0;
    for(std::size_t i = 0; i < std::min(g.rgba.size(), c.rgba.size()); ++i)
    {
      worst = std::max(worst, std::abs(static_cast<int>(g.rgba[i]) - static_cast<int>(c.rgba[i])));
    }
    std::printf("  resample to %dx%d: largest GPU/CPU difference %d level(s)\n", sz[0], sz[1], worst);
    CHECK(worst <= 1);
  }
}

/**
 * @brief CPU-path properties of the resampler: flat colours stay flat, and a
 * black/white checkerboard averages in linear light (sRGB 188, not 128).
 */
void check_resample_properties()
{
  set_opencl_enabled(false);
  Image flat = Image::blank(97, 61, 0);
  for(std::size_t i = 0; i < flat.rgba.size(); i += 4)
  {
    flat.rgba[i] = 200;
    flat.rgba[i + 1] = 100;
    flat.rgba[i + 2] = 30;
    flat.rgba[i + 3] = 255;
  }
  const Image f2 = resample(flat, 40, 25);
  CHECK(f2.valid());
  CHECK(f2.px(20, 12)[0] == 200 && f2.px(20, 12)[1] == 100 && f2.px(20, 12)[2] == 30 && f2.px(20, 12)[3] == 255);
  Image chk = Image::blank(64, 64, 255);
  for(int y = 0; y < 64; ++y)
  {
    for(int x = 0; x < 64; ++x)
    {
      const uint8_t v = ((x + y) % 2 == 0) ? 0 : 255;
      uint8_t* p = chk.px(x, y);
      p[0] = v;
      p[1] = v;
      p[2] = v;
    }
  }
  const Image c2 = resample(chk, 16, 16);
  CHECK(c2.valid());
  std::printf("  checkerboard 4:1 downscale -> %d (linear-light average is 188)\n", c2.px(8, 8)[0]);
  CHECK(std::abs(static_cast<int>(c2.px(8, 8)[0]) - 188) <= 1);
  /* transparent pixels must not darken their neighbours (premultiplied alpha) */
  Image half = Image::blank(32, 32, 0);
  for(int y = 0; y < 32; ++y)
  {
    for(int x = 0; x < 16; ++x)
    {
      uint8_t* p = half.px(x, y);
      p[0] = 255;
      p[1] = 255;
      p[2] = 255;
      p[3] = 255;
    }
  }
  const Image h2 = resample(half, 8, 8);
  CHECK(h2.valid());
  const uint8_t* edge = h2.px(3, 4);
  CHECK(edge[0] == 255 && edge[3] > 0);
}

} // namespace

int main()
{
  Config cfg = default_config();
  cfg.acceleration.opencl = OpenClMode::On; /* also accept CPU OpenCL devices (CI) */
  cfg.acceleration.min_gpu_pixels = 0;
  check_resample_properties();
  const AccelStatus& st = init_acceleration(cfg.acceleration, cfg.threading.max_workers);
  std::printf("OpenCV %s, OpenCL device: %s\n", st.opencv_version.c_str(), st.device.empty() ? "none" : st.device.c_str());
  if(!st.opencl_ok)
  {
    std::printf("OpenCL not usable here (%s): GPU checks skipped\n", st.reason.c_str());
    return test_summary("test_opencl");
  }
  const float bad = cvops::self_test(cfg.acceleration.self_test_tolerance);
  std::printf("  kernel self-test: %.4f %% of samples outside tolerance\n", static_cast<double>(bad) * 100.0);
  CHECK(bad <= 0.001f);
  const Image a = make_scene(kW, kH);
  const Image b = jpeg_roundtrip(a);
  CHECK(b.valid() && b.w == kW && b.h == kH);
  check_heatmaps(a, b, cfg);
  check_stats(a, b, cfg);
  check_resample(a);
  return test_summary("test_opencl");
}
