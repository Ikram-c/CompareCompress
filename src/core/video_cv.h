/**
 * @file video_cv.h
 * @brief Video probing and frame extraction through OpenCV's VideoCapture
 * (AVFoundation on macOS, FFmpeg libraries on Linux).
 *
 * This is the default video path of the macOS build: it needs no external
 * program, decodes H.264 / HEVC with the system's hardware decoder, and falls
 * back to the ffmpeg executable (core/ffmpeg.h) for containers AVFoundation
 * does not read (WebM, MKV, FLV...).  Display rotation is read from the MP4 /
 * MOV track header and applied to every frame when the back-end does not do
 * it itself, so portrait phone videos come out upright on every back-end.
 */
#pragma once

#include "core/config.h"
#include "core/ffmpeg.h"
#include "core/image.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace cc {

/** @brief True when OpenCV was built with at least one back-end that reads video files. */
[[nodiscard]] bool opencv_video_available();

/**
 * @brief Opens a video with VideoCapture and fills in VideoInfo (backend = "OpenCV <name>").
 * The file-size cap is checked by the caller; duration and resolution caps are checked here.
 */
[[nodiscard]] VideoInfo probe_video_opencv(const std::string& path, const VideoLimits& limits);

/**
 * @brief Decodes the frame at `t_seconds` from a video probed by probe_video_opencv().
 * @param info The probe result.
 * @param t_seconds Timestamp, clamped to [0, duration - end_margin_s].
 * @param end_margin_s Never seek closer than this to the end (`ffmpeg.seek_end_margin_s`).
 * @param out Receives the upright frame.
 * @param err Receives a message on failure.
 * @param target_w When > 0 together with target_h, the frame is resampled to this size.
 * @param target_h See target_w.
 */
[[nodiscard]] bool extract_frame_opencv(const VideoInfo& info, double t_seconds, double end_margin_s, Image& out, std::string& err,
                                        int target_w = 0, int target_h = 0);

/** @brief What the MP4 / MOV headers say about the first video track. */
struct IsoTrackInfo
{
  int rotation = 0;  /**< clockwise display rotation: 0, 90, 180 or 270 */
  std::string codec; /**< sample-entry FOURCC, e.g. "avc1", "hvc1"; "" when unknown */
};

/**
 * @brief Rotation and codec of the first video track of an ISO base-media file.
 * @param data The whole `moov` box, or a whole file.
 * @param n Number of bytes.
 */
[[nodiscard]] IsoTrackInfo isobmff_video_track(const uint8_t* data, std::size_t n);

/** @brief isobmff_video_track() of a file on disk (reads only the `moov` box). */
[[nodiscard]] IsoTrackInfo isobmff_file_video_track(const std::string& path);

/**
 * @brief Clockwise display rotation (0, 90, 180, 270) of the first video track
 * of an ISO base-media file (MP4, MOV, M4V, 3GP), from its `tkhd` matrix.
 * @param data The whole `moov` box, or a whole file.
 * @param n Number of bytes.
 * @return The rotation, or 0 when there is none or the data cannot be parsed.
 */
[[nodiscard]] int isobmff_rotation(const uint8_t* data, std::size_t n);

/** @brief isobmff_rotation() of a file on disk (reads only the `moov` box). */
[[nodiscard]] int isobmff_file_rotation(const std::string& path);

} // namespace cc
