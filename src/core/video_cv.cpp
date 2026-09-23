/**
 * @file video_cv.cpp
 * @brief VideoCapture probing / seeking and the MP4 track-header rotation reader.
 */
#include "core/video_cv.h"

#include "util/contract.h"
#include "util/strings.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/videoio/registry.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <list>
#include <vector>

namespace cc {

namespace {

constexpr std::size_t kBoxHeader = 8;
constexpr std::size_t kBoxLargeHeader = 16;
constexpr std::size_t kMaxBoxesPerLevel = 4096;
/** @brief Largest `moov` box read from disk for the rotation (index of a very long clip is a few MB). */
constexpr uint64_t kMaxMoovBytes = static_cast<uint64_t>(64) << 20;
/** @brief tkhd field sizes after version/flags for version 0 and 1 (times, track id, reserved, duration). */
constexpr std::size_t kTkhdTimesV0 = 20;
constexpr std::size_t kTkhdTimesV1 = 32;
/** @brief reserved[2] + layer + alternate_group + volume + reserved: bytes between the times and the matrix. */
constexpr std::size_t kTkhdPreMatrix = 16;
constexpr std::size_t kMatrixBytes = 36;
constexpr std::size_t kHdlrTypeAt = 8;
/** @brief Offset of the first sample entry in an stsd box body (version/flags + entry_count). */
constexpr std::size_t kStsdFirstEntryAt = 8;
constexpr int kFullTurn = 360;
constexpr int kQuarterTurn = 90;
constexpr int kHalfTurn = 180;
constexpr int kThreeQuarterTurn = 270;
constexpr double kRadToDeg = 57.29577951308232;
constexpr double kMsPerSecond = 1000.0;
constexpr int kBitsPerByte = 8;
/** @brief VideoCaptures kept open per thread (the original and the site copy while sampling). */
constexpr std::size_t kCaptureCacheSize = 2;
/** @brief Seek retries a few frames earlier when the last frame cannot be read. */
constexpr int kSeekRetries = 3;
constexpr double kFallbackStepS = 0.04;
constexpr int kFourccBytes = 4;
constexpr int kByteBits = 8;
constexpr int kByteMask = 0xFF;

/** @brief Shift of the most significant byte of a 32-bit value. */
constexpr unsigned kTopByteShift = 24;
/** @brief Length of a box type code. */
constexpr std::size_t kBoxTypeLength = 4;
/** @brief First non-printable code after the printable ASCII range. */
constexpr char kAsciiDelete = 127;

uint32_t be32(const uint8_t* p)
{
  return (static_cast<uint32_t>(p[0]) << kTopByteShift) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

uint64_t be64(const uint8_t* p) { return (static_cast<uint64_t>(be32(p)) << 32) | be32(p + 4); }

/** @brief One ISO base-media box inside a byte range. */
struct Box
{
  char type[kBoxTypeLength + 1] = {};
  std::size_t body = 0; /**< first byte after the header */
  std::size_t end = 0;  /**< one past the last byte */
};

/** @brief Reads the box header at `pos`; false at the end of the range or on a malformed size. */
bool read_box(const uint8_t* d, std::size_t pos, std::size_t end, Box& b)
{
  if(pos >= end || end - pos < kBoxHeader)
  {
    return false;
  }
  uint64_t size = be32(d + pos);
  std::size_t header = kBoxHeader;
  if(size == 1)
  {
    if(end - pos < kBoxLargeHeader)
    {
      return false;
    }
    size = be64(d + pos + kBoxHeader);
    header = kBoxLargeHeader;
  }
  else if(size == 0)
  {
    size = end - pos;
  }
  if(size < header || size > end - pos)
  {
    return false;
  }
  std::memcpy(b.type, d + pos + 4, 4);
  b.body = pos + header;
  b.end = pos + static_cast<std::size_t>(size);
  return true;
}

/** @brief Clockwise rotation encoded in a tkhd box body, or -1 when it is malformed. */
int tkhd_rotation(const uint8_t* d, const Box& tkhd)
{
  if(tkhd.end - tkhd.body < 4)
  {
    return -1;
  }
  const std::size_t times = d[tkhd.body] == 1 ? kTkhdTimesV1 : kTkhdTimesV0;
  const std::size_t m = tkhd.body + 4 + times + kTkhdPreMatrix;
  if(m + kMatrixBytes > tkhd.end)
  {
    return -1;
  }
  /* matrix a, b (first row); 16.16 fixed point, the scale cancels in atan2 */
  const double a = static_cast<double>(static_cast<int32_t>(be32(d + m)));
  const double b = static_cast<double>(static_cast<int32_t>(be32(d + m + 4)));
  if(a == 0.0 && b == 0.0)
  {
    return 0;
  }
  const double deg = std::atan2(b, a) * kRadToDeg;
  int r = static_cast<int>(std::lround(deg / kQuarterTurn)) * kQuarterTurn;
  r = ((r % kFullTurn) + kFullTurn) % kFullTurn;
  return r;
}

/** @brief Finds the first child box of `type` inside `parent`. */
bool find_child(const uint8_t* d, const Box& parent, const char* type, Box& out)
{
  std::size_t pos = parent.body;
  for(std::size_t i = 0; i < kMaxBoxesPerLevel; ++i)
  {
    if(!read_box(d, pos, parent.end, out))
    {
      return false;
    }
    if(std::memcmp(out.type, type, 4) == 0)
    {
      return true;
    }
    pos = out.end;
  }
  return false;
}

/** @brief True when the mdia box's handler is a video handler ('vide'). */
bool mdia_is_video(const uint8_t* d, const Box& mdia)
{
  Box hdlr;
  return find_child(d, mdia, "hdlr", hdlr) && hdlr.end - hdlr.body >= kHdlrTypeAt + 4
         && std::memcmp(d + hdlr.body + kHdlrTypeAt, "vide", 4) == 0;
}

/** @brief Codec FOURCC of the first sample entry (mdia/minf/stbl/stsd), e.g. "avc1"; "" when absent. */
std::string mdia_codec(const uint8_t* d, const Box& mdia)
{
  Box minf;
  Box stbl;
  Box stsd;
  if(!find_child(d, mdia, "minf", minf) || !find_child(d, minf, "stbl", stbl) || !find_child(d, stbl, "stsd", stsd))
  {
    return std::string();
  }
  /* stsd: version/flags (4), entry_count (4), then the first entry box: size (4), format (4) */
  if(stsd.end - stsd.body < kStsdFirstEntryAt + kBoxHeader)
  {
    return std::string();
  }
  const uint8_t* f = d + stsd.body + kStsdFirstEntryAt + 4;
  return std::string(reinterpret_cast<const char*>(f), 4);
}

/** @brief Rotation and codec of a trak if it is a video track (rotation -1 otherwise). */
IsoTrackInfo trak_video_info(const uint8_t* d, const Box& trak)
{
  IsoTrackInfo info;
  info.rotation = -1;
  Box tkhd;
  Box mdia;
  if(!find_child(d, trak, "mdia", mdia) || !mdia_is_video(d, mdia))
  {
    return info;
  }
  info.rotation = find_child(d, trak, "tkhd", tkhd) ? std::max(0, tkhd_rotation(d, tkhd)) : 0;
  info.codec = mdia_codec(d, mdia);
  return info;
}

/** @brief The first video trak inside a moov box. */
IsoTrackInfo moov_video_info(const uint8_t* d, const Box& moov)
{
  std::size_t pos = moov.body;
  for(std::size_t i = 0; i < kMaxBoxesPerLevel; ++i)
  {
    Box b;
    if(!read_box(d, pos, moov.end, b))
    {
      break;
    }
    if(std::memcmp(b.type, "trak", 4) == 0)
    {
      const IsoTrackInfo t = trak_video_info(d, b);
      if(t.rotation >= 0)
      {
        return t;
      }
    }
    pos = b.end;
  }
  return IsoTrackInfo{};
}

/* ---- VideoCapture ------------------------------------------------------ */

/** @brief An opened capture remembered per thread so that scrubbing does not reopen the file. */
struct OpenCapture
{
  std::string path;
  cv::VideoCapture cap;
};

/** @brief Most recently used first; a list keeps element addresses stable. */
thread_local std::list<OpenCapture> t_captures;

/** @brief Opens (or reuses) a capture for `path`; nullptr with `err` set on failure. */
cv::VideoCapture* capture_for(const std::string& path, std::string& err)
{
  for(auto it = t_captures.begin(); it != t_captures.end(); ++it)
  {
    if(it->path == path && it->cap.isOpened())
    {
      t_captures.splice(t_captures.begin(), t_captures, it);
      return &t_captures.front().cap;
    }
  }
  t_captures.emplace_front();
  OpenCapture& oc = t_captures.front();
  oc.path = path;
  bool opened = false;
  try
  {
    opened = oc.cap.open(path, cv::CAP_ANY);
    if(!opened)
    {
      err = "OpenCV could not open the video (no back-end reads this container)";
    }
  }
  catch(const cv::Exception& e)
  {
    err = std::string("OpenCV: ") + e.what();
  }
  if(!opened)
  {
    t_captures.pop_front();
    return nullptr;
  }
  while(t_captures.size() > kCaptureCacheSize)
  {
    t_captures.pop_back();
  }
  return &t_captures.front().cap;
}

/** @brief Closes a cached capture (a new probe of the same path may be a different file). */
void forget_capture(const std::string& path)
{
  t_captures.remove_if([&](const OpenCapture& c) { return c.path == path; });
}

/** @brief Packs a four-character code the way OpenCV's CAP_PROP_FOURCC does (first character in the low byte). */
int fourcc_value(const std::string& four)
{
  unsigned v = 0;
  for(std::size_t i = 0; i < 4 && i < four.size(); ++i)
  {
    v |= static_cast<unsigned>(static_cast<unsigned char>(four[i])) << (i * kByteBits);
  }
  return static_cast<int>(v);
}

/** @brief "h264", "hevc", ... from a FOURCC. */
std::string codec_from_fourcc(int fourcc)
{
  std::string s;
  for(int i = 0; i < kFourccBytes; ++i)
  {
    const char c = static_cast<char>((static_cast<unsigned>(fourcc) >> (i * kByteBits)) & kByteMask);
    if(c > ' ' && c < kAsciiDelete)
    {
      s += c;
    }
  }
  const std::string l = to_lower(s);
  if(l == "avc1" || l == "h264" || l == "x264" || l == "avc3")
  {
    return "h264";
  }
  if(l == "hvc1" || l == "hev1" || l == "hevc" || l == "h265")
  {
    return "hevc";
  }
  if(l == "av01")
  {
    return "av1";
  }
  if(l == "vp09")
  {
    return "vp9";
  }
  if(l == "vp08")
  {
    return "vp8";
  }
  return l.empty() ? std::string("?") : l;
}

cv::RotateFlags rotate_flag(int degrees_cw)
{
  if(degrees_cw == kQuarterTurn)
  {
    return cv::ROTATE_90_CLOCKWISE;
  }
  if(degrees_cw == kHalfTurn)
  {
    return cv::ROTATE_180;
  }
  return cv::ROTATE_90_COUNTERCLOCKWISE;
}

/** @brief BGR(A) frame, rotated upright, to an RGBA Image. */
bool frame_to_image(const cv::Mat& frame, int rotation, Image& out)
{
  if(frame.empty() || frame.depth() != CV_8U)
  {
    return false;
  }
  cv::Mat upright;
  if(rotation == kQuarterTurn || rotation == kHalfTurn || rotation == kThreeQuarterTurn)
  {
    cv::rotate(frame, upright, rotate_flag(rotation));
  }
  else
  {
    upright = frame;
  }
  Image img = Image::blank(upright.cols, upright.rows, kOpaqueAlpha);
  cv::Mat dst(img.h, img.w, CV_8UC4, img.rgba.data());
  switch(upright.channels())
  {
    case 1: cv::cvtColor(upright, dst, cv::COLOR_GRAY2RGBA); break;
    case 3: cv::cvtColor(upright, dst, cv::COLOR_BGR2RGBA); break;
    case 4: cv::cvtColor(upright, dst, cv::COLOR_BGRA2RGBA); break;
    default: return false;
  }
  out = std::move(img);
  return true;
}

/** @brief Seeks and reads; retries slightly earlier when the requested frame is past the last decodable one. */
bool read_at(cv::VideoCapture& cap, double t, double step, cv::Mat& frame)
{
  double tt = t;
  for(int i = 0; i <= kSeekRetries; ++i)
  {
    cap.set(cv::CAP_PROP_POS_MSEC, tt * kMsPerSecond);
    if(cap.read(frame) && !frame.empty())
    {
      return true;
    }
    tt = std::max(0.0, tt - step);
  }
  return false;
}

/** @brief OpenCV's name for the back-end that opened a capture ("AVFOUNDATION", "FFMPEG", ...). */
std::string backend_name(const cv::VideoCapture& cap)
{
  try
  {
    return cap.getBackendName();
  }
  catch(const cv::Exception&)
  {
    return "?";
  }
}

/** @brief Degrees normalised to [0, 360). */
int normalise_degrees(int d) { return ((d % kFullTurn) + kFullTurn) % kFullTurn; }

/**
 * @brief Codec, frame rate, duration and rotation.  The FFmpeg back-end
 * reports the codec and rotates frames itself (CAP_PROP_ORIENTATION_AUTO);
 * AVFoundation does neither (its CAP_PROP_FOURCC is the output pixel format),
 * so for it both come from the MP4/MOV track header and the rotation is
 * applied to every frame here.
 */
void fill_stream_info(cv::VideoCapture& cap, const std::string& backend, VideoInfo& v)
{
  v.container = to_lower(file_extension(v.path));
  v.fps = cap.get(cv::CAP_PROP_FPS);
  const double frames = cap.get(cv::CAP_PROP_FRAME_COUNT);
  v.nb_frames = frames > 0 ? static_cast<int64_t>(std::llround(frames)) : 0;
  v.duration_s = (v.fps > 0 && frames > 0) ? frames / v.fps : 0.0;
  const int meta = normalise_degrees(static_cast<int>(std::lround(cap.get(cv::CAP_PROP_ORIENTATION_META))));
  int clockwise = meta;
  if(backend == "FFMPEG")
  {
    v.codec = codec_from_fourcc(static_cast<int>(cap.get(cv::CAP_PROP_FOURCC)));
    v.frame_rotation = 0;
  }
  else
  {
    const IsoTrackInfo track = isobmff_file_video_track(v.path);
    v.codec = track.codec.empty() ? std::string("?") : codec_from_fourcc(fourcc_value(track.codec));
    clockwise = track.rotation != 0 ? track.rotation : meta;
    v.frame_rotation = clockwise;
  }
  /* VideoInfo::rotation uses ffprobe's convention (display-matrix degrees, counter-clockwise) */
  v.rotation = (kFullTurn - clockwise) % kFullTurn;
}

/** @brief Applies the duration and resolution caps. */
void apply_limits(const VideoLimits& limits, VideoInfo& v)
{
  if(v.duration_s > limits.max_duration_s)
  {
    v.ok = false;
    v.error = "video is " + std::to_string(static_cast<int>(std::lround(v.duration_s))) + " s long, above the "
              + std::to_string(static_cast<int>(limits.max_duration_s)) + " s limit (limits.video.max_duration_s)";
  }
  else if(std::max(v.width, v.height) > limits.max_edge_px)
  {
    v.ok = false;
    v.error = "video is " + std::to_string(v.width) + "x" + std::to_string(v.height) + ", above the " + std::to_string(limits.max_edge_px)
              + " px limit per edge (limits.video.max_edge_px)";
  }
}

} // namespace

IsoTrackInfo isobmff_video_track(const uint8_t* data, std::size_t n)
{
  if(data == nullptr || n < kBoxHeader)
  {
    return IsoTrackInfo{};
  }
  std::size_t pos = 0;
  for(std::size_t i = 0; i < kMaxBoxesPerLevel; ++i)
  {
    Box b;
    if(!read_box(data, pos, n, b))
    {
      break;
    }
    if(std::memcmp(b.type, "moov", 4) == 0)
    {
      return moov_video_info(data, b);
    }
    pos = b.end;
  }
  return IsoTrackInfo{};
}

int isobmff_rotation(const uint8_t* data, std::size_t n) { return isobmff_video_track(data, n).rotation; }

IsoTrackInfo isobmff_file_video_track(const std::string& path)
{
  std::ifstream f(path, std::ios::binary);
  if(!f)
  {
    return IsoTrackInfo{};
  }
  f.seekg(0, std::ios::end);
  const std::streamoff file_end = f.tellg();
  if(file_end <= 0)
  {
    return IsoTrackInfo{};
  }
  uint64_t pos = 0;
  const uint64_t total = static_cast<uint64_t>(file_end);
  for(std::size_t i = 0; i < kMaxBoxesPerLevel && pos + kBoxHeader <= total; ++i)
  {
    uint8_t hdr[kBoxLargeHeader] = {};
    f.seekg(static_cast<std::streamoff>(pos));
    const std::size_t want = static_cast<std::size_t>(std::min<uint64_t>(kBoxLargeHeader, total - pos));
    if(!f.read(reinterpret_cast<char*>(hdr), static_cast<std::streamsize>(want)))
    {
      return IsoTrackInfo{};
    }
    uint64_t size = be32(hdr);
    if(size == 1)
    {
      if(want < kBoxLargeHeader)
      {
        return IsoTrackInfo{};
      }
      size = be64(hdr + kBoxHeader);
    }
    else if(size == 0)
    {
      size = total - pos;
    }
    if(size < kBoxHeader || size > total - pos)
    {
      return IsoTrackInfo{};
    }
    if(std::memcmp(hdr + 4, "moov", 4) == 0)
    {
      if(size > kMaxMoovBytes)
      {
        return IsoTrackInfo{};
      }
      std::vector<uint8_t> moov(static_cast<std::size_t>(size));
      f.seekg(static_cast<std::streamoff>(pos));
      if(!f.read(reinterpret_cast<char*>(moov.data()), static_cast<std::streamsize>(moov.size())))
      {
        return IsoTrackInfo{};
      }
      return isobmff_video_track(moov.data(), moov.size());
    }
    pos += size;
  }
  return IsoTrackInfo{};
}

int isobmff_file_rotation(const std::string& path) { return isobmff_file_video_track(path).rotation; }

bool opencv_video_available()
{
  for(const cv::VideoCaptureAPIs api : cv::videoio_registry::getStreamBackends())
  {
    if(cv::videoio_registry::hasBackend(api) && api != cv::CAP_IMAGES && api != cv::CAP_OPENCV_MJPEG)
    {
      return true;
    }
  }
  return false;
}

VideoInfo probe_video_opencv(const std::string& path, const VideoLimits& limits)
{
  VideoInfo v;
  v.path = path;
  std::string err;
  forget_capture(path);
  cv::VideoCapture* cap = capture_for(path, err);
  if(cap == nullptr)
  {
    v.error = err;
    return v;
  }
  const std::string backend = backend_name(*cap);
  cv::Mat first;
  if(!cap->read(first) || first.empty())
  {
    v.error = "OpenCV (" + backend + ") opened the file but decoded no frame";
    return v;
  }
  v.backend = "OpenCV " + backend;
  fill_stream_info(*cap, backend, v);
  const bool swap = v.frame_rotation == kQuarterTurn || v.frame_rotation == kThreeQuarterTurn;
  v.width = swap ? first.rows : first.cols;
  v.height = swap ? first.cols : first.rows;
  uint64_t size = 0;
  if(file_size(path, size))
  {
    v.size_bytes = size;
    if(v.duration_s > 0)
    {
      v.bit_rate = static_cast<int64_t>(static_cast<double>(size) * kBitsPerByte / v.duration_s);
    }
  }
  v.ok = v.width > 0 && v.height > 0;
  if(v.ok)
  {
    apply_limits(limits, v);
  }
  return v;
}

bool extract_frame_opencv(const VideoInfo& info, double t_seconds, double end_margin_s, Image& out, std::string& err, int target_w,
                          int target_h)
{
  if(!info.ok)
  {
    err = info.error;
    return false;
  }
  cv::VideoCapture* cap = capture_for(info.path, err);
  if(cap == nullptr)
  {
    return false;
  }
  const double t = std::clamp(t_seconds, 0.0, std::max(0.0, info.duration_s - end_margin_s));
  const double step = info.fps > 0 ? 1.0 / info.fps : kFallbackStepS;
  cv::Mat frame;
  bool ok = false;
  try
  {
    ok = read_at(*cap, t, step, frame);
  }
  catch(const cv::Exception& e)
  {
    err = std::string("OpenCV: ") + e.what();
    return false;
  }
  Image img;
  if(!ok || !frame_to_image(frame, info.frame_rotation, img))
  {
    char buf[64];
    std::snprintf(buf, sizeof buf, "no frame could be decoded at %.3f s", t);
    err = buf;
    return false;
  }
  if(target_w > 0 && target_h > 0 && (img.w != target_w || img.h != target_h))
  {
    img = resample(img, target_w, target_h);
  }
  if(!img.valid())
  {
    err = "frame resampling failed";
    return false;
  }
  out = std::move(img);
  return true;
}

} // namespace cc
