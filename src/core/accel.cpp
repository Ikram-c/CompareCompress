/**
 * @file accel.cpp
 * @brief OpenCV start-up: thread pool, OpenCL device, kernel build and self-test.
 */
#include "core/accel.h"

#include "core/cv_ops.h"

#include <opencv2/core.hpp>
#include <opencv2/core/ocl.hpp>
#include <opencv2/videoio/registry.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace cc {

namespace {

/** @brief Largest fraction of self-test samples that may differ from the CPU result. */
constexpr float kMaxSelfTestMismatch = 0.001f;
/** @brief Percent factor for messages. */
constexpr double kPercent = 100.0;

std::mutex g_init_mutex;
AccelStatus g_status;
std::atomic<bool> g_enabled{false};
std::atomic<long> g_min_gpu_pixels{0};
std::atomic<int> g_video_backend{static_cast<int>(VideoBackend::Auto)};

struct KeyName
{
  const char* key;
  int value;
};

const KeyName kOpenClKeys[] = {{"auto", static_cast<int>(OpenClMode::Auto)},
                               {"on", static_cast<int>(OpenClMode::On)},
                               {"off", static_cast<int>(OpenClMode::Off)}};
const KeyName kVideoKeys[] = {{"auto", static_cast<int>(VideoBackend::Auto)},
                              {"opencv", static_cast<int>(VideoBackend::OpenCV)},
                              {"ffmpeg", static_cast<int>(VideoBackend::Ffmpeg)}};

template <std::size_t N>
bool parse_key(const KeyName (&table)[N], const std::string& key, int& out)
{
  for(const KeyName& k : table)
  {
    if(key == k.key)
    {
      out = k.value;
      return true;
    }
  }
  return false;
}

template <std::size_t N>
const char* key_of(const KeyName (&table)[N], int value)
{
  for(const KeyName& k : table)
  {
    if(k.value == value)
    {
      return k.key;
    }
  }
  return "?";
}

/** @brief SIMD features OpenCV's dispatcher can use on this CPU. */
std::string cpu_feature_list()
{
  struct Feature
  {
    int id;
    const char* name;
  };
  const Feature features[] = {{CV_CPU_SSE4_2, "SSE4.2"}, {CV_CPU_AVX, "AVX"},       {CV_CPU_AVX2, "AVX2"},
                              {CV_CPU_FMA3, "FMA3"},     {CV_CPU_AVX_512F, "AVX-512"}, {CV_CPU_NEON, "NEON"}};
  std::string out;
  for(const Feature& f : features)
  {
    if(cv::checkHardwareSupport(f.id))
    {
      out += out.empty() ? "" : " ";
      out += f.name;
    }
  }
  return out.empty() ? std::string("baseline") : out;
}

/** @brief OpenCV video back-ends that can open files, e.g. "AVFoundation". */
std::string video_backend_list()
{
  std::string out;
  for(const cv::VideoCaptureAPIs api : cv::videoio_registry::getStreamBackends())
  {
    /* skip plugin slots that are not present and the image-sequence / MJPEG readers */
    if(!cv::videoio_registry::hasBackend(api) || api == cv::CAP_IMAGES || api == cv::CAP_OPENCV_MJPEG)
    {
      continue;
    }
    out += out.empty() ? "" : ", ";
    out += cv::videoio_registry::getBackendName(api);
  }
  return out.empty() ? std::string("none") : out;
}

void set_env(const char* name, const std::string& value)
{
#ifdef _WIN32
  (void)_putenv_s(name, value.c_str());
#else
  (void)setenv(name, value.c_str(), 1);
#endif
}

/**
 * @brief Chooses the device, builds and (in auto mode) tests the kernels.
 * @return true when OpenCL may be used; `st.reason` explains a false.
 */
bool bring_up_opencl(const AccelerationConfig& cfg, AccelStatus& st)
{
  if(cfg.opencl == OpenClMode::Off)
  {
    st.reason = "switched off (acceleration.opencl: off)";
    return false;
  }
  if(!cfg.opencl_device.empty())
  {
    /* read by OpenCV when it creates its default context, i.e. on first use below */
    set_env("OPENCV_OPENCL_DEVICE", cfg.opencl_device);
  }
  if(!cv::ocl::haveOpenCL())
  {
    st.reason = "no OpenCL runtime found";
    return false;
  }
  cv::ocl::setUseOpenCL(true);
  const cv::ocl::Device& dev = cv::ocl::Device::getDefault();
  if(dev.ptr() == nullptr || !dev.available())
  {
    st.reason = "no usable OpenCL device";
    return false;
  }
  st.opencl_runtime = true;
  st.device = dev.name() + " - " + dev.version() + " (" + dev.vendorName() + ")";
  if(cfg.opencl == OpenClMode::Auto && dev.type() == cv::ocl::Device::TYPE_CPU)
  {
    st.reason = "the only OpenCL device is a CPU; OpenCV's own CPU path is faster (set acceleration.opencl: on to force it)";
    return false;
  }
  std::string err;
  if(!cvops::build_kernels(err))
  {
    st.reason = err;
    return false;
  }
  if(cfg.opencl == OpenClMode::Auto)
  {
    const float bad = cvops::self_test(cfg.self_test_tolerance);
    if(bad > kMaxSelfTestMismatch)
    {
      char msg[128];
      std::snprintf(msg, sizeof msg, "self-test failed: %.2f %% of the GPU results differ from the CPU", static_cast<double>(bad) * kPercent);
      st.reason = msg;
      return false;
    }
  }
  return true;
}

} // namespace

bool opencl_mode_from_key(const std::string& key, OpenClMode& out)
{
  int v = 0;
  if(!parse_key(kOpenClKeys, key, v))
  {
    return false;
  }
  out = static_cast<OpenClMode>(v);
  return true;
}

bool video_backend_from_key(const std::string& key, VideoBackend& out)
{
  int v = 0;
  if(!parse_key(kVideoKeys, key, v))
  {
    return false;
  }
  out = static_cast<VideoBackend>(v);
  return true;
}

const char* opencl_mode_key(OpenClMode m) { return key_of(kOpenClKeys, static_cast<int>(m)); }

const char* video_backend_key(VideoBackend b) { return key_of(kVideoKeys, static_cast<int>(b)); }

const AccelStatus& init_acceleration(const AccelerationConfig& cfg, int max_threads)
{
  const std::lock_guard<std::mutex> lock(g_init_mutex);
  if(g_status.initialised)
  {
    return g_status;
  }
  AccelStatus st;
  st.opencv_version = CV_VERSION;
  cv::setNumThreads(std::max(1, std::min(max_threads, cv::getNumberOfCPUs())));
  st.cpu_threads = cv::getNumThreads();
  st.cpu_features = cpu_feature_list();
  st.video_io = video_backend_list();
#ifdef __APPLE__
  st.still_decoders = "OpenCV imgcodecs (JPEG, PNG, WebP, GIF, BMP, TIFF), macOS ImageIO (HEIC, HEIF, AVIF on macOS 13+)";
#else
  st.still_decoders = "OpenCV imgcodecs (JPEG, PNG, WebP, GIF, BMP, TIFF)";
#endif
  st.opencl_ok = bring_up_opencl(cfg, st);
  cv::ocl::setUseOpenCL(st.opencl_ok);
  g_min_gpu_pixels = cfg.min_gpu_pixels;
  g_video_backend = static_cast<int>(cfg.video_backend);
  g_enabled = st.opencl_ok;
  st.initialised = true;
  g_status = st;
  return g_status;
}

const AccelStatus& acceleration_status() { return g_status; }

bool set_opencl_enabled(bool on)
{
  g_enabled = on && g_status.opencl_ok;
  return g_enabled.load();
}

bool opencl_enabled() { return g_enabled.load(); }

bool use_gpu_for(std::size_t pixels)
{
  const bool on = g_enabled.load() && pixels >= static_cast<std::size_t>(std::max(0L, g_min_gpu_pixels.load()));
  cv::ocl::setUseOpenCL(on);
  return on;
}

VideoBackend configured_video_backend() { return static_cast<VideoBackend>(g_video_backend.load()); }

} // namespace cc
