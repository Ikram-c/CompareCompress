/**
 * @file test_ffprobe.cpp
 * @brief ffprobe output parsing, plus an end-to-end video round trip when
 * ffmpeg is installed on the machine running the tests.
 */
#include "core/config.h"
#include "core/ffmpeg.h"
#include "core/process.h"
#include "test_util.h"

#include <cstdio>
#include <filesystem>
#include <string>

using namespace cc;

namespace {

/** @brief Parser on captured ffprobe output (stream section first, then format). */
void check_parser()
{
  const char* sample = "codec_name=h264\nwidth=320\nheight=240\npix_fmt=yuv420p\nr_frame_rate=10/1\navg_frame_rate=10/1\n"
                       "duration=3.000000\nbit_rate=169642\nnb_frames=30\n"
                       "format_name=mov,mp4,m4a,3gp,3g2,mj2\nduration=3.000000\nsize=64811\nbit_rate=172829\n";
  const VideoInfo v = parse_ffprobe_output(sample);
  CHECK(v.ok);
  CHECK_EQ_STR(v.codec, "h264");
  CHECK(v.width == 320 && v.height == 240);
  CHECK_NEAR(v.fps, 10.0, 1e-9);
  CHECK_NEAR(v.duration_s, 3.0, 1e-9);
  CHECK(v.nb_frames == 30);
  CHECK(v.bit_rate == 169642);
  CHECK(v.size_bytes == 64811);
  CHECK(v.summary().find("h264 320x240") == 0);

  /* stream values missing (e.g. webm) -> fall back to the format section */
  const char* webm = "codec_name=vp9\nwidth=640\nheight=360\nr_frame_rate=30000/1001\navg_frame_rate=0/0\n"
                     "format_name=matroska,webm\nduration=12.5\nsize=1000\nbit_rate=640000\n";
  const VideoInfo w = parse_ffprobe_output(webm);
  CHECK(w.ok);
  CHECK_NEAR(w.fps, 29.97, 1e-2);
  CHECK_NEAR(w.duration_s, 12.5, 1e-9);
  CHECK(w.bit_rate == 640000);

  CHECK(!parse_ffprobe_output("format_name=mp3\n").ok);

  /* rotation side data (phone videos): the displayed size is swapped */
  const VideoInfo rot = parse_ffprobe_output("codec_name=h264\nwidth=1920\nheight=1080\nrotation=-90\nduration=2\n");
  CHECK(rot.width == 1080 && rot.height == 1920 && rot.rotation == 270);
  const VideoInfo rot2 = parse_ffprobe_output("codec_name=h264\nwidth=1920\nheight=1080\nTAG:rotate=180\nduration=2\n");
  CHECK(rot2.width == 1920 && rot2.height == 1080 && rot2.rotation == 180);
}

/** @brief Generates a clip, probes it, extracts frames and checks the limits. */
void check_end_to_end(const Config& cfg, const FfmpegTools& tools)
{
  const std::string path = (std::filesystem::temp_directory_path() / "cc_test_video.mp4").string();
  const ProcessResult gen = run_process({tools.ffmpeg, "-v", "error", "-y", "-f", "lavfi", "-i", "testsrc2=duration=2:size=160x120:rate=10",
                                         "-c:v", "libx264", "-pix_fmt", "yuv420p", path},
                                        cfg.process, 0, cfg.process.default_timeout_ms);
  if(!gen.succeeded())
  {
    std::printf("could not generate a test video (%s); skipping\n", gen.err.c_str());
    return;
  }
  const VideoLimits limits = cfg.limits.video;
  const VideoInfo info = probe_video(tools, path, limits);
  CHECK(info.ok);
  CHECK(info.width == 160 && info.height == 120);
  CHECK_NEAR(info.duration_s, 2.0, 0.2);
  CHECK(info.size_bytes > 0);

  Image frame;
  std::string err;
  CHECK(extract_frame(tools, info, 1.0, frame, err));
  CHECK(frame.w == 160 && frame.h == 120);
  Image scaled;
  CHECK(extract_frame(tools, info, 0.5, scaled, err, 80, 60));
  CHECK(scaled.w == 80 && scaled.h == 60);

  /* limits are enforced */
  VideoLimits tiny = limits;
  tiny.max_bytes = 10;
  CHECK(!probe_video(tools, path, tiny).ok);
  VideoLimits shortlim = limits;
  shortlim.max_duration_s = 1.0;
  CHECK(!probe_video(tools, path, shortlim).ok);
  VideoLimits smallres = limits;
  smallres.max_edge_px = 100;
  CHECK(!probe_video(tools, path, smallres).ok);

  /* a bogus path fails cleanly */
  CHECK(!probe_video(tools, path + ".missing", limits).ok);

  /* rotated container: ffmpeg auto-rotates, the size must follow */
  const std::string rpath = (std::filesystem::temp_directory_path() / "cc_test_video_rot.mp4").string();
  const ProcessResult rgen = run_process({tools.ffmpeg, "-v", "error", "-y", "-display_rotation", "90", "-i", path, "-c", "copy", rpath},
                                         cfg.process, 0, cfg.process.default_timeout_ms);
  if(rgen.succeeded())
  {
    const VideoInfo rinfo = probe_video(tools, rpath, limits);
    CHECK(rinfo.ok);
    CHECK(rinfo.rotation == 90);
    CHECK(rinfo.width == 120 && rinfo.height == 160);
    Image rframe;
    CHECK(extract_frame(tools, rinfo, 0.5, rframe, err));
    CHECK(rframe.w == 120 && rframe.h == 160);
    std::remove(rpath.c_str());
  }
  else
  {
    std::printf("ffmpeg does not support -display_rotation here; skipping the rotation test\n");
  }
  std::remove(path.c_str());
}

} // namespace

int main()
{
  const Config& cfg = default_config();
  check_parser();

  /* run_process reports a missing program without throwing */
  const ProcessResult missing = run_process({"cc-no-such-program-xyz"}, cfg.process, 0, cfg.process.version_timeout_ms);
  CHECK(!missing.succeeded());
  CHECK(!missing.failure.empty());

  const FfmpegTools tools = locate_ffmpeg(cfg.ffmpeg, cfg.process, VideoBackend::Ffmpeg); /* this test is about ffprobe */
  if(!tools.available())
  {
    std::printf("ffmpeg not found; skipping the end-to-end video test\n");
    return test_summary("test_ffprobe");
  }
  std::printf("using %s\n", tools.version.c_str());
  check_end_to_end(cfg, tools);
  return test_summary("test_ffprobe");
}
