/**
 * @file cc_bench.cpp
 * @brief Times the image pipeline on the CPU and through OpenCL, so the
 * `acceleration` settings can be chosen for a particular Mac.
 *
 * Usage:  cc_bench [samples/original.png]
 * Builds a 12-megapixel pair from the image (a phone photo and a 1080 px
 * "site" copy) and times resampling, every heat map and the statistics, once
 * with OpenCL off and once with it on (when a device is available).
 */
#include "core/accel.h"
#include "core/config.h"
#include "core/cv_ops.h"
#include "core/image.h"
#include "core/metrics.h"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace cc;

namespace {

double now_s() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

/** @brief A "site" copy: JPEG at quality 70, like a feed image. */
Image jpeg_copy(const Image& src)
{
  const cv::Mat rgba(src.h, src.w, CV_8UC4, const_cast<uint8_t*>(src.rgba.data()));
  cv::Mat bgr;
  cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
  std::vector<uint8_t> jpg;
  cv::imencode(".jpg", bgr, jpg, {cv::IMWRITE_JPEG_QUALITY, 70});
  Image out;
  (void)decode_image(jpg.data(), jpg.size(), 65536, out);
  return out;
}

void run_pass(const char* label, const Image& big, const Config& cfg)
{
  std::printf("\n%s\n", label);
  double t = now_s();
  const Image site = jpeg_copy(resample(big, 1080, 810));
  std::printf("  resample 4000x3000 -> 1080x810       %7.3f s  (+ JPEG q70 copy)\n", now_s() - t);
  t = now_s();
  const Image orig = fit_cover(big, site.w, site.h);
  std::printf("  fit_cover (comparison size)          %7.3f s\n", now_s() - t);
  for(int m = 0; m < kMetricCount; ++m)
  {
    t = now_s();
    const HeatMap h = compute_heatmap(orig, site, static_cast<Metric>(m), cfg.metrics, cfg.threading);
    std::printf("  heat map %-28s %7.3f s\n", metric_name(static_cast<Metric>(m)), now_s() - t);
  }
  t = now_s();
  const PairStats s = compute_stats(orig, site, cfg.metrics, cfg.threading);
  std::printf("  statistics (PSNR %.2f dB, SSIM %.4f)  %7.3f s\n", s.psnr, s.ssim, now_s() - t);
  const Image site_big = resample(site, big.w, big.h);
  t = now_s();
  const HeatMap full = compute_heatmap(big, site_big, Metric::DeltaE2000, cfg.metrics, cfg.threading);
  std::printf("  dE2000 heat map at 4000x3000         %7.3f s\n", now_s() - t);
}

} // namespace

int main(int argc, char** argv)
{
  const std::string path = argc > 1 ? argv[1] : "samples/original.png";
  Config cfg = default_config();
  cfg.acceleration.opencl = OpenClMode::On; /* measure any device; the self-test result is printed below */
  cfg.acceleration.min_gpu_pixels = 0;
  const AccelStatus& st = init_acceleration(cfg.acceleration, cfg.threading.max_workers);
  std::printf("OpenCV %s, %d threads (%s)\nOpenCL: %s%s\n", st.opencv_version.c_str(), st.cpu_threads, st.cpu_features.c_str(),
              st.device.empty() ? "no device" : st.device.c_str(), st.opencl_ok ? "" : (" - off: " + st.reason).c_str());
  Image src;
  const LoadResult r = load_image_file(path, cfg.limits, src);
  if(!r.ok)
  {
    std::fprintf(stderr, "%s: %s\n", path.c_str(), r.error.c_str());
    return 1;
  }
  set_opencl_enabled(false);
  const Image big = resample(src, 4000, 3000);
  run_pass("CPU (OpenCV SIMD + thread pool)", big, cfg);
  if(set_opencl_enabled(true))
  {
    std::printf("\nkernel self-test: %.4f %% of values differ from the CPU (auto mode accepts <= 0.1 %%)\n",
                static_cast<double>(cvops::self_test(cfg.acceleration.self_test_tolerance)) * 100.0);
    run_pass("OpenCL", big, cfg);
    std::printf("\n%lu kernel launches on the device\n", cvops::gpu_launches());
  }
  return 0;
}
