/**
 * @file accel.h
 * @brief OpenCV / OpenCL start-up, status and the switch every accelerated
 * operation consults.
 *
 * The image pipeline is written against `cv::UMat` (OpenCV's Transparent API),
 * so the same code runs on the GPU through OpenCL when it is enabled and on
 * the CPU (with OpenCV's SIMD and thread pool) when it is not.  The few
 * operations OpenCV has no function for (the colour-difference metrics, the
 * SSIM combination, the linear-light conversions) are OpenCL kernels in
 * `core/cl_kernels.h` with a CPU twin in `core/cv_ops.cpp`.
 *
 * In `auto` mode OpenCL is only switched on after a self-test: the kernels
 * must build and must reproduce the CPU results on a small test pattern.  An
 * old or buggy driver therefore degrades to the CPU path instead of drawing
 * wrong heat maps.
 */
#pragma once

#include <cstddef>
#include <string>

namespace cc {

/** @brief The `acceleration.opencl` setting. */
enum class OpenClMode
{
  Auto, /**< use OpenCL when a device passes the self-test */
  On,   /**< use OpenCL whenever the kernels build (skip the comparison) */
  Off   /**< never use OpenCL */
};

/** @brief The `acceleration.video_backend` setting. */
enum class VideoBackend
{
  Auto,   /**< OpenCV VideoCapture (AVFoundation on macOS) first, the ffmpeg executable as fallback */
  OpenCV, /**< OpenCV VideoCapture only */
  Ffmpeg  /**< the ffmpeg / ffprobe executables only */
};

/** @brief Parses `auto` / `on` / `off`. */
bool opencl_mode_from_key(const std::string& key, OpenClMode& out);

/** @brief Parses `auto` / `opencv` / `ffmpeg`. */
bool video_backend_from_key(const std::string& key, VideoBackend& out);

/** @brief Config key of a mode. */
const char* opencl_mode_key(OpenClMode m);

/** @brief Config key of a backend. */
const char* video_backend_key(VideoBackend b);

/** @brief The `acceleration` section of config.yaml. */
struct AccelerationConfig
{
  OpenClMode opencl = OpenClMode::Auto;
  std::string opencl_device;              /**< OPENCV_OPENCL_DEVICE selector; empty = OpenCV's default */
  long min_gpu_pixels = 0;                /**< smaller images stay on the CPU (upload cost dominates) */
  VideoBackend video_backend = VideoBackend::Auto;
  float self_test_tolerance = 0;          /**< largest accepted |GPU - CPU| difference in the self-test */
};

/** @brief What start-up found; shown in Settings and printed by --version. */
struct AccelStatus
{
  bool initialised = false;
  std::string opencv_version;   /**< e.g. "4.12.0" */
  bool opencl_runtime = false;  /**< OpenCV found an OpenCL platform and device */
  bool opencl_ok = false;       /**< kernels built (and, in auto mode, passed the self-test) */
  std::string device;           /**< "Intel(R) HD Graphics 6000 - OpenCL 1.2 (Apple)" or "" */
  std::string reason;           /**< why OpenCL is off, when it is */
  std::string video_io;         /**< OpenCV video back-ends compiled in, e.g. "AVFoundation" */
  std::string still_decoders;   /**< image decoders available, e.g. "OpenCV imgcodecs, macOS ImageIO" */
  int cpu_threads = 0;          /**< threads OpenCV's parallel_for_ may use */
  std::string cpu_features;     /**< SIMD features OpenCV dispatches to, e.g. "SSE4_2 AVX AVX2" */
};

/**
 * @brief Initialises OpenCV: thread count, OpenCL device selection, kernel
 * build and self-test.  Call once from the main thread before any image work;
 * later calls return the first result.
 * @param cfg `acceleration` section.
 * @param max_threads `threading.max_workers`.
 */
const AccelStatus& init_acceleration(const AccelerationConfig& cfg, int max_threads);

/** @brief The status recorded by init_acceleration() (not initialised = all false). */
const AccelStatus& acceleration_status();

/**
 * @brief Runtime switch from the Settings window.  Only takes effect when
 * the kernels built (AccelStatus::opencl_ok); returns the resulting state.
 */
bool set_opencl_enabled(bool on);

/** @brief True when OpenCL is currently in use for new work. */
bool opencl_enabled();

/**
 * @brief Decides for one operation and applies the decision to the calling
 * thread (OpenCV's OpenCL switch is per thread).
 * @param pixels Size of the image the operation works on.
 * @return true when the operation runs through OpenCL.
 */
bool use_gpu_for(std::size_t pixels);

/** @brief The configured video back-end (Auto before init_acceleration()). */
VideoBackend configured_video_backend();

} // namespace cc
