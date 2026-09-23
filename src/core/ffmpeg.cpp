/**
 * @file ffmpeg.cpp
 * @brief ffprobe parsing and ffmpeg frame extraction.
 */
#include "core/ffmpeg.h"

#include "core/process.h"
#include "core/video_cv.h"
#include "util/contract.h"
#include "util/strings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <utility>
#include <vector>

namespace cc {

namespace {

/** @brief Bytes per pixel of the raw rgb24 frames ffmpeg writes. */
constexpr std::size_t kRgbBytesPerPixel = 3;
/** @brief Degrees in a full turn, for normalising rotation metadata. */
constexpr int kFullTurn = 360;
/** @brief Bits per kilobit in the summary line. */
constexpr int64_t kBitsPerKilobit = 1000;
/** @brief Rotations that swap width and height. */
constexpr int kQuarterTurn = 90;
constexpr int kThreeQuarterTurn = 270;

/** @brief Base of the numbers ffprobe prints. */
constexpr int kDecimalBase = 10;

/** @brief strtod with "" and garbage mapped to 0 (ffprobe prints "N/A" for unknowns, already filtered). */
double parse_double(const std::string& s)
{
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  return end != s.c_str() ? v : 0.0;
}

/** @brief strtoll with "" and garbage mapped to 0. */
int64_t parse_int64(const std::string& s)
{
  char* end = nullptr;
  const long long v = std::strtoll(s.c_str(), &end, kDecimalBase);
  return end != s.c_str() ? static_cast<int64_t>(v) : 0;
}

/** @brief Parses ffprobe rates such as "30000/1001", "25/1" or "0/0". */
double parse_rate(const std::string& s)
{
  const std::size_t slash = s.find('/');
  if(slash == std::string::npos)
  {
    return parse_double(s);
  }
  const double a = parse_double(s.substr(0, slash));
  const double b = parse_double(s.substr(slash + 1));
  return b > 0 ? a / b : 0.0;
}

/** @brief Every ffprobe field probe_video() asks for. */
const char* const kProbeEntries = "stream=codec_name,width,height,pix_fmt,r_frame_rate,avg_frame_rate,nb_frames,bit_rate,duration"
                                  ":stream_side_data=rotation:stream_tags=rotate:format=format_name,duration,size,bit_rate";

/** @brief key=value lines of an ffprobe run: first and last value of every key. */
struct KeyValues
{
  std::map<std::string, std::string> first;
  std::map<std::string, std::string> last;

  [[nodiscard]] std::string get(const char* key) const
  {
    const auto it = first.find(key);
    return it != first.end() ? it->second : std::string();
  }
  [[nodiscard]] std::string get_last(const char* key) const
  {
    const auto it = last.find(key);
    return it != last.end() ? it->second : std::string();
  }
};

/**
 * @brief Splits ffprobe output into key/value maps.
 *
 * ffprobe prints the stream section first, then the format section; keys
 * repeat (duration, bit_rate), so the first (stream) value is kept and the
 * last (format) one serves as fallback when the stream value is missing or "N/A".
 */
KeyValues parse_key_values(const std::string& text)
{
  KeyValues kv;
  for(const std::string& raw : split(text, '\n'))
  {
    const std::string line = trim(raw);
    const std::size_t eq = line.find('=');
    if(eq == std::string::npos)
    {
      continue;
    }
    const std::string key = line.substr(0, eq);
    const std::string value = line.substr(eq + 1);
    if(value.empty() || value == "N/A")
    {
      continue;
    }
    kv.first.emplace(key, value); /* keeps the first */
    kv.last[key] = value;
  }
  return kv;
}

/** @brief Applies the container's display rotation to the reported size. */
void apply_rotation(const KeyValues& kv, VideoInfo& v)
{
  std::string rot = kv.get("rotation");
  if(rot.empty())
  {
    rot = kv.get("TAG:rotate");
  }
  if(rot.empty())
  {
    return;
  }
  int r = static_cast<int>(std::lround(parse_double(rot)));
  r = ((r % kFullTurn) + kFullTurn) % kFullTurn;
  v.rotation = r;
  if(r == kQuarterTurn || r == kThreeQuarterTurn)
  {
    std::swap(v.width, v.height);
  }
}

/** @brief Converts a raw rgb24 buffer into an Image. */
bool rgb24_to_image(const std::vector<uint8_t>& raw, int w, int h, Image& out)
{
  if(w <= 0 || h <= 0)
  {
    return false;
  }
  const std::size_t pixels = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
  if(raw.size() < pixels * kRgbBytesPerPixel)
  {
    return false;
  }
  Image img = Image::blank(w, h, kOpaqueAlpha);
  const uint8_t* s = raw.data();
  uint8_t* d = img.rgba.data();
  for(std::size_t i = 0; i < pixels; ++i)
  {
    d[i * kImageChannels + 0] = s[i * kRgbBytesPerPixel + 0];
    d[i * kImageChannels + 1] = s[i * kRgbBytesPerPixel + 1];
    d[i * kImageChannels + 2] = s[i * kRgbBytesPerPixel + 2];
  }
  out = std::move(img);
  return true;
}

/** @brief Largest stdout a raw frame of w x h may produce. */
uint64_t frame_cap_bytes(int w, int h, const ProcessConfig& process)
{
  return static_cast<uint64_t>(w) * static_cast<uint64_t>(h) * kRgbBytesPerPixel + process.frame_cap_slack_bytes;
}

/** @brief "ffprobe failed: <stderr>" style message. */
std::string tool_error(const std::string& prefix, const ProcessResult& r)
{
  const std::string detail = trim(r.err.empty() ? r.failure : r.err);
  return detail.empty() ? prefix : prefix + ": " + detail;
}

} // namespace

std::string VideoInfo::summary() const
{
  if(!ok)
  {
    return error;
  }
  const std::string rate = bit_rate > 0 ? std::to_string(bit_rate / kBitsPerKilobit) + " kb/s" : std::string("unknown bitrate");
  char buf[256];
  std::snprintf(buf, sizeof buf, "%s %dx%d @ %.3g fps, %.1f s, %s", codec.c_str(), width, height, fps, duration_s, rate.c_str());
  return buf;
}

FfmpegTools locate_ffmpeg(const FfmpegConfig& cfg, const ProcessConfig& process, VideoBackend backend)
{
  FfmpegTools t;
  t.config = cfg;
  t.process = process;
  t.backend = backend;
  t.opencv_video = opencv_video_available();
  t.ffmpeg = find_program("ffmpeg", cfg);
  t.ffprobe = find_program("ffprobe", cfg);
  if(!t.ffmpeg.empty())
  {
    const ProcessResult r = run_process({t.ffmpeg, "-version"}, process, process.version_stdout_cap_bytes, process.version_timeout_ms);
    if(r.started && !r.out.empty())
    {
      const std::string s(r.out.begin(), r.out.end());
      t.version = s.substr(0, s.find('\n'));
    }
  }
  return t;
}

VideoInfo parse_ffprobe_output(const std::string& text)
{
  VideoInfo v;
  const KeyValues kv = parse_key_values(text);
  v.codec = kv.get("codec_name");
  v.pix_fmt = kv.get("pix_fmt");
  v.container = kv.get("format_name");
  v.width = static_cast<int>(parse_int64(kv.get("width")));
  v.height = static_cast<int>(parse_int64(kv.get("height")));
  /* Phone videos carry a display rotation; ffmpeg auto-rotates frames, so the
   * size handed around must be the displayed one. */
  apply_rotation(kv, v);
  v.fps = parse_rate(kv.get("avg_frame_rate"));
  if(v.fps <= 0)
  {
    v.fps = parse_rate(kv.get("r_frame_rate"));
  }
  v.duration_s = parse_double(kv.get("duration"));
  if(v.duration_s <= 0)
  {
    v.duration_s = parse_double(kv.get_last("duration"));
  }
  v.nb_frames = parse_int64(kv.get("nb_frames"));
  v.bit_rate = parse_int64(kv.get("bit_rate"));
  if(v.bit_rate <= 0)
  {
    v.bit_rate = parse_int64(kv.get_last("bit_rate"));
  }
  const std::string size = kv.get_last("size");
  if(!size.empty())
  {
    v.size_bytes = static_cast<uint64_t>(std::max<int64_t>(0, parse_int64(size)));
  }
  v.ok = v.width > 0 && v.height > 0;
  if(!v.ok)
  {
    v.error = "ffprobe found no video stream";
  }
  return v;
}

namespace {

/** @brief Applies the duration and resolution caps to a parsed probe. */
void apply_video_limits(const VideoLimits& limits, VideoInfo& parsed)
{
  if(parsed.duration_s > limits.max_duration_s)
  {
    parsed.ok = false;
    parsed.error = "video is " + std::to_string(static_cast<int>(std::lround(parsed.duration_s))) + " s long, above the "
                   + std::to_string(static_cast<int>(limits.max_duration_s)) + " s limit (limits.video.max_duration_s)";
    return;
  }
  if(std::max(parsed.width, parsed.height) > limits.max_edge_px)
  {
    parsed.ok = false;
    parsed.error = "video is " + std::to_string(parsed.width) + "x" + std::to_string(parsed.height) + ", above the "
                   + std::to_string(limits.max_edge_px) + " px limit per edge (limits.video.max_edge_px)";
  }
}

} // namespace

namespace {

/** @brief The ffprobe half of probe_video(); the size cap has been checked. */
VideoInfo probe_with_ffprobe(const FfmpegTools& tools, const std::string& path, uint64_t size, const VideoLimits& limits);

} // namespace

VideoInfo probe_video(const FfmpegTools& tools, const std::string& path, const VideoLimits& limits)
{
  VideoInfo v;
  v.path = path;
  if(!tools.video_available())
  {
    v.error = "no video back-end: OpenCV has no video reader in this build and ffmpeg was not found (brew/port install ffmpeg)";
    return v;
  }
  uint64_t size = 0;
  if(!file_size(path, size))
  {
    v.error = "cannot read '" + path + "'";
    return v;
  }
  if(size > limits.max_bytes)
  {
    v.error = "video is " + format_bytes(size) + ", above the " + format_bytes(limits.max_bytes)
              + " limit (limits.video.max_bytes in config.yaml)";
    return v;
  }
  std::string opencv_error;
  if(tools.use_opencv())
  {
    VideoInfo cvi = probe_video_opencv(path, limits);
    if(cvi.ok || !tools.use_ffmpeg())
    {
      cvi.size_bytes = size;
      return cvi;
    }
    opencv_error = cvi.error;
  }
  VideoInfo fv = probe_with_ffprobe(tools, path, size, limits);
  if(!fv.ok && !opencv_error.empty())
  {
    fv.error = opencv_error + "; ffprobe: " + fv.error;
  }
  return fv;
}

namespace {

VideoInfo probe_with_ffprobe(const FfmpegTools& tools, const std::string& path, uint64_t size, const VideoLimits& limits)
{
  VideoInfo v;
  v.path = path;
  const ProcessResult r = run_process({tools.ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries", kProbeEntries, "-of",
                                       "default=noprint_wrappers=1", path},
                                      tools.process, tools.process.probe_stdout_cap_bytes, tools.process.probe_timeout_ms);
  if(!r.started)
  {
    v.error = r.failure;
    return v;
  }
  if(r.exit_code != 0)
  {
    v.error = tool_error("ffprobe failed", r);
    return v;
  }
  VideoInfo parsed = parse_ffprobe_output(std::string(r.out.begin(), r.out.end()));
  parsed.path = path;
  parsed.size_bytes = size;
  parsed.backend = "ffmpeg";
  if(parsed.ok)
  {
    apply_video_limits(limits, parsed);
  }
  return parsed;
}

} // namespace

bool extract_frame(const FfmpegTools& tools, const VideoInfo& info, double t_seconds, Image& out, std::string& err, int target_w,
                   int target_h)
{
  if(info.backend.rfind("OpenCV", 0) == 0)
  {
    return extract_frame_opencv(info, t_seconds, tools.config.seek_end_margin_s, out, err, target_w, target_h);
  }
  if(!tools.available())
  {
    err = "ffmpeg not found";
    return false;
  }
  if(!info.ok)
  {
    err = info.error;
    return false;
  }
  const double t = std::clamp(t_seconds, 0.0, std::max(0.0, info.duration_s - tools.config.seek_end_margin_s));
  char tbuf[32];
  std::snprintf(tbuf, sizeof tbuf, "%.3f", t);
  const bool scale = target_w > 0 && target_h > 0;
  const int w = scale ? target_w : info.width;
  const int h = scale ? target_h : info.height;
  std::vector<std::string> argv = {tools.ffmpeg, "-v", "error", "-nostdin", "-ss", tbuf, "-i", info.path, "-map", "0:v:0",
                                   "-frames:v", "1", "-an", "-sn"};
  if(scale)
  {
    argv.emplace_back("-vf");
    argv.push_back("scale=" + std::to_string(w) + ":" + std::to_string(h) + ":flags=" + tools.config.scale_filter);
  }
  argv.insert(argv.end(), {"-f", "rawvideo", "-pix_fmt", "rgb24", "-"});
  const ProcessResult r = run_process(argv, tools.process, frame_cap_bytes(w, h, tools.process), tools.process.default_timeout_ms);
  if(!r.started)
  {
    err = r.failure;
    return false;
  }
  if(!rgb24_to_image(r.out, w, h, out))
  {
    err = tool_error("ffmpeg returned no frame at " + std::string(tbuf) + " s", r);
    return false;
  }
  return true;
}

bool decode_still_with_ffmpeg(const FfmpegTools& tools, const std::string& path, Image& out, std::string& err)
{
  if(!tools.available())
  {
    err = "ffmpeg not found";
    return false;
  }
  /* First ask ffprobe for the size (an image is a one-frame "video" to ffmpeg). */
  const ProcessResult p = run_process({tools.ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height",
                                       "-of", "default=noprint_wrappers=1", path},
                                      tools.process, tools.process.version_stdout_cap_bytes, tools.process.probe_timeout_ms);
  if(!p.started || p.exit_code != 0)
  {
    err = tool_error("ffprobe could not read the file", p);
    return false;
  }
  const VideoInfo vi = parse_ffprobe_output(std::string(p.out.begin(), p.out.end()));
  if(!vi.ok || vi.width > tools.config.max_still_edge_px || vi.height > tools.config.max_still_edge_px)
  {
    err = "unsupported image size (ffmpeg.max_still_edge_px)";
    return false;
  }
  const ProcessResult r = run_process({tools.ffmpeg, "-v", "error", "-nostdin", "-i", path, "-map", "0:v:0", "-frames:v", "1", "-f",
                                       "rawvideo", "-pix_fmt", "rgb24", "-"},
                                      tools.process, frame_cap_bytes(vi.width, vi.height, tools.process),
                                      tools.process.default_timeout_ms);
  if(!r.started || !rgb24_to_image(r.out, vi.width, vi.height, out))
  {
    err = tool_error("ffmpeg could not decode the image", r);
    return false;
  }
  return true;
}

} // namespace cc
