/**
 * @file test_video.cpp
 * @brief MP4 track-header rotation parsing, and the OpenCV video path on the
 * bundled sample clips: probing, seeking, scaling, and (when the ffmpeg
 * executable is installed) frame-accurate agreement with ffmpeg, including a
 * portrait clip carrying a display rotation.
 */
#include "core/config.h"
#include "core/ffmpeg.h"
#include "core/image.h"
#include "core/process.h"
#include "core/video_cv.h"
#include "test_util.h"

#include <opencv2/core.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace cc;

namespace {

using Bytes = std::vector<uint8_t>;

void put32(Bytes& b, uint32_t v)
{
  b.push_back(static_cast<uint8_t>(v >> 24));
  b.push_back(static_cast<uint8_t>(v >> 16));
  b.push_back(static_cast<uint8_t>(v >> 8));
  b.push_back(static_cast<uint8_t>(v));
}

Bytes box(const char* type, const Bytes& payload)
{
  Bytes b;
  put32(b, static_cast<uint32_t>(payload.size() + 8));
  b.insert(b.end(), type, type + 4);
  b.insert(b.end(), payload.begin(), payload.end());
  return b;
}

Bytes cat(const Bytes& a, const Bytes& b)
{
  Bytes r = a;
  r.insert(r.end(), b.begin(), b.end());
  return r;
}

/** @brief A version-0 tkhd whose matrix rotates by `deg` clockwise (0, 90, 180, 270). */
Bytes tkhd(int deg)
{
  const uint32_t one = 0x00010000U;
  const uint32_t minus_one = 0xFFFF0000U;
  uint32_t a = one, b = 0, c = 0, d = one;
  if(deg == 90)
  {
    a = 0;
    b = one;
    c = minus_one;
    d = 0;
  }
  else if(deg == 180)
  {
    a = minus_one;
    d = minus_one;
  }
  else if(deg == 270)
  {
    a = 0;
    b = minus_one;
    c = one;
    d = 0;
  }
  Bytes p;
  put32(p, 3);                     /* version 0, flags */
  for(int i = 0; i < 5; ++i)       /* creation, modification, track id, reserved, duration */
  {
    put32(p, 0);
  }
  for(int i = 0; i < 4; ++i)       /* reserved[2], layer+alternate_group, volume+reserved */
  {
    put32(p, 0);
  }
  const uint32_t m[9] = {a, b, 0, c, d, 0, 0, 0, 0x40000000U};
  for(const uint32_t v : m)
  {
    put32(p, v);
  }
  put32(p, 640U << 16);
  put32(p, 360U << 16);
  return box("tkhd", p);
}

Bytes hdlr(const char* handler)
{
  Bytes p;
  put32(p, 0);
  put32(p, 0);
  p.insert(p.end(), handler, handler + 4);
  for(int i = 0; i < 3; ++i)
  {
    put32(p, 0);
  }
  p.push_back(0);
  return box("hdlr", p);
}

Bytes trak(int deg, const char* handler) { return box("trak", cat(tkhd(deg), box("mdia", hdlr(handler)))); }

void check_rotation_parser()
{
  Bytes ftyp = box("ftyp", Bytes{'i', 's', 'o', 'm', 0, 0, 2, 0});
  /* an mdat with a 64-bit size in front of moov, like a non-faststart file */
  Bytes mdat;
  put32(mdat, 1);
  mdat.insert(mdat.end(), {'m', 'd', 'a', 't'});
  put32(mdat, 0);
  put32(mdat, 20);
  put32(mdat, 0xDEADBEEF);
  for(const int deg : {0, 90, 180, 270})
  {
    /* an audio track with another matrix comes first and must be ignored */
    const Bytes moov = box("moov", cat(trak(180, "soun"), trak(deg, "vide")));
    const Bytes file = cat(cat(ftyp, mdat), moov);
    CHECK(isobmff_rotation(file.data(), file.size()) == deg);
    CHECK(isobmff_rotation(moov.data(), moov.size()) == deg);
  }
  const Bytes no_video = box("moov", trak(90, "soun"));
  CHECK(isobmff_rotation(no_video.data(), no_video.size()) == 0);
  Bytes truncated = box("moov", trak(90, "vide"));
  truncated.resize(truncated.size() - 30);
  CHECK(isobmff_rotation(truncated.data(), truncated.size()) == 0);
  CHECK(isobmff_rotation(nullptr, 0) == 0);
}

/** @brief Mean absolute difference over RGB of two same-size images, or -1. */
double mean_abs_diff(const Image& a, const Image& b)
{
  if(!a.valid() || !b.valid() || a.w != b.w || a.h != b.h)
  {
    return -1.0;
  }
  double sum = 0;
  for(std::size_t i = 0; i < a.rgba.size(); i += 4)
  {
    for(std::size_t c = 0; c < 3; ++c)
    {
      sum += std::abs(static_cast<int>(a.rgba[i + c]) - static_cast<int>(b.rgba[i + c]));
    }
  }
  return sum / static_cast<double>(a.rgba.size() / 4 * 3);
}

void check_samples(const Config& cfg)
{
  const std::string dir = CC_SAMPLES_DIR;
  if(!opencv_video_available())
  {
    std::printf("OpenCV has no video back-end in this build: sample checks skipped\n");
    return;
  }
  const VideoInfo v = probe_video_opencv(dir + "/original.mp4", cfg.limits.video);
  std::printf("  original.mp4: %s via %s, rotation %d\n", v.summary().c_str(), v.backend.c_str(), v.rotation);
  CHECK(v.ok);
  CHECK(v.width == 640 && v.height == 360);
  CHECK(v.fps > 20 && v.fps < 61);
  CHECK(v.duration_s > 3.5 && v.duration_s < 4.5);
  CHECK_EQ_STR(v.codec.c_str(), "h264");
  Image f0;
  Image f2;
  Image f_end;
  Image small;
  std::string err;
  CHECK(extract_frame_opencv(v, 0.0, cfg.ffmpeg.seek_end_margin_s, f0, err));
  CHECK(extract_frame_opencv(v, 2.0, cfg.ffmpeg.seek_end_margin_s, f2, err));
  CHECK(extract_frame_opencv(v, 99.0, cfg.ffmpeg.seek_end_margin_s, f_end, err)); /* clamped to the end */
  CHECK(extract_frame_opencv(v, 1.0, cfg.ffmpeg.seek_end_margin_s, small, err, 320, 180));
  CHECK(f0.w == 640 && f0.h == 360 && f2.valid() && f_end.valid());
  CHECK(small.w == 320 && small.h == 180);
  CHECK(mean_abs_diff(f0, f2) > 1.0); /* testsrc2 moves: different timestamps give different frames */

  const VideoInfo t = probe_video_opencv(dir + "/tiktok.mp4", cfg.limits.video);
  CHECK(t.ok && t.width == 480 && t.height == 270);
  /* the track-header reader the AVFoundation path relies on for codec and rotation */
  const IsoTrackInfo track = isobmff_file_video_track(dir + "/original.mp4");
  CHECK_EQ_STR(track.codec.c_str(), "avc1");
  CHECK(track.rotation == 0);
}

/** @brief Frame-accurate agreement with the ffmpeg executable, when it is installed. */
void check_against_ffmpeg(const Config& cfg)
{
  const std::string dir = CC_SAMPLES_DIR;
  const FfmpegTools tools = locate_ffmpeg(cfg.ffmpeg, cfg.process, VideoBackend::Ffmpeg);
  if(!opencv_video_available() || !tools.available())
  {
    std::printf("  ffmpeg executable or OpenCV video not available: cross-check with ffmpeg skipped\n");
    return;
  }
  std::string err;
  const VideoInfo v = probe_video_opencv(dir + "/original.mp4", cfg.limits.video);
  const VideoInfo fv = probe_video(tools, dir + "/original.mp4", cfg.limits.video);
  CHECK(fv.ok && fv.backend == "ffmpeg");
  for(const double ts : {0.0, 1.0, 2.0, 3.2})
  {
    Image a;
    Image b;
    CHECK(extract_frame(tools, fv, ts, a, err));
    CHECK(extract_frame_opencv(v, ts, cfg.ffmpeg.seek_end_margin_s, b, err));
    const double d = mean_abs_diff(a, b);
    std::printf("  t=%.1f s: OpenCV vs ffmpeg frame, mean |diff| %.2f\n", ts, d);
    CHECK(d >= 0.0 && d < 3.0);
  }
}

/** @brief A portrait clip (the sample with a 90-degree display rotation) comes out upright on both paths. */
void check_rotated_clip(const Config& cfg)
{
  const std::string dir = CC_SAMPLES_DIR;
  const FfmpegTools tools = locate_ffmpeg(cfg.ffmpeg, cfg.process, VideoBackend::Ffmpeg);
  if(!opencv_video_available() || !tools.available())
  {
    return;
  }
  std::string err;
  const VideoInfo v = probe_video_opencv(dir + "/original.mp4", cfg.limits.video);
  const std::string rotated = std::string(std::getenv("TMPDIR") != nullptr ? std::getenv("TMPDIR") : "/tmp") + "/cc_rotated_test.mp4";
  const ProcessResult r = run_process({tools.ffmpeg, "-v", "error", "-y", "-display_rotation", "90", "-i", dir + "/original.mp4", "-c",
                                       "copy", rotated},
                                      cfg.process, cfg.process.probe_stdout_cap_bytes, cfg.process.default_timeout_ms);
  if(!r.started || r.exit_code != 0)
  {
    std::printf("  this ffmpeg cannot write display rotation: rotation cross-check skipped\n");
    return;
  }
  const VideoInfo rf = probe_video(tools, rotated, cfg.limits.video);
  const VideoInfo rc = probe_video_opencv(rotated, cfg.limits.video);
  const int parsed = isobmff_file_rotation(rotated);
  std::printf("  rotated clip: ffprobe %dx%d rot %d, OpenCV %dx%d rot %d, tkhd %d\n", rf.width, rf.height, rf.rotation, rc.width, rc.height,
              rc.rotation, parsed);
  CHECK(rf.ok && rc.ok);
  CHECK(rc.width == rf.width && rc.height == rf.height);
  CHECK(rc.width == 360 && rc.height == 640);
  CHECK(rc.rotation == rf.rotation); /* both report ffprobe's convention */
  Image upright_ffmpeg;
  Image upright_opencv;
  CHECK(extract_frame(tools, rf, 2.0, upright_ffmpeg, err));
  CHECK(extract_frame_opencv(rc, 2.0, cfg.ffmpeg.seek_end_margin_s, upright_opencv, err));
  const double d_auto = mean_abs_diff(upright_ffmpeg, upright_opencv);
  std::printf("  rotated clip, back-end rotation: mean |diff| %.2f\n", d_auto);
  CHECK(d_auto >= 0.0 && d_auto < 3.0);
  /* The manual path used with AVFoundation: rotate an unrotated frame by the tkhd value. */
  VideoInfo manual = v;
  manual.frame_rotation = parsed;
  Image upright_manual;
  CHECK(extract_frame_opencv(manual, 2.0, cfg.ffmpeg.seek_end_margin_s, upright_manual, err));
  const double d_manual = mean_abs_diff(upright_ffmpeg, upright_manual);
  std::printf("  rotated clip, tkhd rotation applied by the app: mean |diff| %.2f\n", d_manual);
  CHECK(d_manual >= 0.0 && d_manual < 3.0);
  std::remove(rotated.c_str());
}

} // namespace

int main()
{
  check_rotation_parser();
  const Config& cfg = default_config();
  check_samples(cfg);
  check_against_ffmpeg(cfg);
  check_rotated_clip(cfg);
  return test_summary("test_video");
}
