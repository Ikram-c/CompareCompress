/**
 * @file ffmpeg.h
 * @brief Video access: OpenCV VideoCapture (core/video_cv.h) first, the
 * ffprobe / ffmpeg executables as fallback (`acceleration.video_backend`).
 *
 * Videos are never decoded whole: the probe reports the metadata (used to
 * enforce the size / duration / resolution caps) and single frames are
 * decoded at the timestamps the user scrubs to.  With the ffmpeg executables,
 * ffprobe does the probing and ffmpeg pipes frames out as raw RGB.
 */
#pragma once

#include "core/accel.h"
#include "core/config.h"
#include "core/image.h"

#include <cstdint>
#include <string>

namespace cc {

/** @brief What the probe reported about a video. */
struct VideoInfo
{
  bool ok = false;
  std::string error;
  std::string path;
  std::string backend; /**< "OpenCV AVFOUNDATION", "OpenCV FFMPEG" or "ffmpeg" */
  std::string codec;
  std::string pix_fmt;
  std::string container;
  int width = 0;  /**< displayed size (rotation metadata already applied) */
  int height = 0;
  int rotation = 0; /**< display rotation from the container as ffprobe reports it (counter-clockwise degrees: 0, 90, 180, 270) */
  int frame_rotation = 0; /**< clockwise rotation still to apply to decoded frames (back-ends that do not rotate) */
  double fps = 0;
  double duration_s = 0;
  int64_t nb_frames = 0;
  int64_t bit_rate = 0;
  uint64_t size_bytes = 0;

  /** @brief One line such as "h264 1920x1080 @ 30 fps, 12.0 s, 4200 kb/s", or the error. */
  [[nodiscard]] std::string summary() const;
};

/** @brief Locations of the tools, the video back-end choice and the configuration every call needs. */
struct FfmpegTools
{
  std::string ffmpeg;  /**< full path, "" if missing */
  std::string ffprobe; /**< full path, "" if missing */
  std::string version; /**< first line of `ffmpeg -version` */
  VideoBackend backend = VideoBackend::Auto; /**< `acceleration.video_backend` */
  bool opencv_video = false;                 /**< OpenCV has a back-end that reads files */
  FfmpegConfig config;
  ProcessConfig process;

  /** @brief True when both programs were found. */
  [[nodiscard]] bool available() const { return !ffmpeg.empty() && !ffprobe.empty(); }

  /** @brief True when OpenCV may be used for video. */
  [[nodiscard]] bool use_opencv() const { return opencv_video && backend != VideoBackend::Ffmpeg; }

  /** @brief True when the ffmpeg executables may be used. */
  [[nodiscard]] bool use_ffmpeg() const { return available() && backend != VideoBackend::OpenCV; }

  /** @brief True when videos can be opened at all. */
  [[nodiscard]] bool video_available() const { return use_opencv() || use_ffmpeg(); }
};

/**
 * @brief Finds ffmpeg and ffprobe, reads the ffmpeg version and records the video back-end choice.
 * @param cfg `ffmpeg` section (search directories, scaler, time margins).
 * @param process `process` section (time-outs and output caps).
 * @param backend `acceleration.video_backend`.
 */
[[nodiscard]] FfmpegTools locate_ffmpeg(const FfmpegConfig& cfg, const ProcessConfig& process,
                                        VideoBackend backend = VideoBackend::Auto);

/**
 * @brief Parses ffprobe's `-of default=noprint_wrappers=1` key=value output.
 * @return `ok == false` with an error when no video stream was described.
 */
[[nodiscard]] VideoInfo parse_ffprobe_output(const std::string& text);

/**
 * @brief Probes a video: OpenCV first (unless the back-end is `ffmpeg`), then ffprobe.
 *
 * Enforces `limits` (file size before probing, duration and resolution
 * afterwards) and returns a descriptive error when one is exceeded.
 */
[[nodiscard]] VideoInfo probe_video(const FfmpegTools& tools, const std::string& path, const VideoLimits& limits);

/**
 * @brief Decodes one frame with the back-end that probed the video.
 * @param tools The located tools.
 * @param info A successful probe of the video.
 * @param t_seconds Timestamp, clamped to the duration.
 * @param out Receives the frame.
 * @param err Receives a message on failure.
 * @param target_w When > 0 together with target_h, the frame is scaled to this size.
 * @param target_h See target_w.
 */
[[nodiscard]] bool extract_frame(const FfmpegTools& tools, const VideoInfo& info, double t_seconds, Image& out, std::string& err,
                                 int target_w = 0, int target_h = 0);

/**
 * @brief Fallback still-image decoder for formats the built-in decoders cannot read (AVIF, HEIC... on Linux).
 */
[[nodiscard]] bool decode_still_with_ffmpeg(const FfmpegTools& tools, const std::string& path, Image& out, std::string& err);

} // namespace cc
