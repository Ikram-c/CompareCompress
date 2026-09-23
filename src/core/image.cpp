/**
 * @file image.cpp
 * @brief File I/O, sniffing, decoding (OpenCV imgcodecs, macOS ImageIO),
 * sRGB-aware resampling (cv::resize in linear light) and PNG export.
 */
#include "core/image.h"

#include "util/contract.h"
#include "util/strings.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

#include "core/accel.h"
#include "core/cv_ops.h"
#include "core/jpeg_info.h"
#ifdef __APPLE__
#include "core/imageio_apple.h"
#endif

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <climits>
#include <cstdlib>

namespace cc {

namespace {

/** @brief Bytes of the JPEG start-of-image signature checked by the sniffer. */
constexpr std::size_t kSniffMinBytes = 4;
/** @brief Offset of the RIFF form type ("WEBP", "AVI "). */
constexpr std::size_t kRiffTypeOffset = 8;
/** @brief Bytes a RIFF header needs before the form type can be read. */
constexpr std::size_t kRiffHeaderBytes = 12;
/** @brief Offset of "ftyp" in an ISO base-media file. */
constexpr std::size_t kFtypOffset = 4;
/** @brief Offset of the major brand in an ISO base-media file. */
constexpr std::size_t kBrandOffset = 8;
/** @brief Length of a brand code. */
constexpr std::size_t kBrandLength = 4;
/** @brief Size of a BMP file header; "BM" alone is too short a signature. */
constexpr std::size_t kBmpHeaderBytes = 14;
/** @brief Characters examined when looking for an HTML prologue. */
constexpr std::size_t kHtmlProbeChars = 16;
/** @brief Length of the UTF-8 byte-order mark. */
constexpr std::size_t kBomBytes = 3;
/** @brief Length of the GIF87a / GIF89a signatures. */
constexpr std::size_t kGifSignatureBytes = 6;

/** @brief Video container extensions that have no signature the sniffer can trust. */
const char* const kVideoExtensions[] = {"mp4", "mov", "m4v", "webm", "mkv", "avi", "ts",  "m2ts",
                                        "mts", "flv", "wmv", "3gp",  "mpg", "mpeg", "ogv", "gifv"};

/** @brief Major brands of HEIF/HEIC files. */
const char* const kHeifBrands[] = {"heic", "heix", "hevc", "hevx", "mif1", "msf1", "heim", "heis"};

/** @brief Converts a UTF-8 path to a std::filesystem::path. */
std::filesystem::path to_path(const std::string& utf8)
{
#ifdef _WIN32
  return std::filesystem::u8path(utf8);
#else
  return std::filesystem::path(utf8);
#endif
}

/**
 * @brief fopen() with a UTF-8 path on every platform.
 * @return The stream, or nullptr.
 */
FILE* open_file(const std::string& utf8, const char* mode)
{
  CC_REQUIRE(mode != nullptr, return nullptr);
#ifdef _WIN32
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
  if(n <= 0)
  {
    return nullptr;
  }
  std::wstring wide(static_cast<std::size_t>(n), L'\0');
  if(MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], n) <= 0)
  {
    return nullptr;
  }
  const std::wstring wide_mode(mode, mode + std::strlen(mode));
  return _wfopen(wide.c_str(), wide_mode.c_str());
#else
  return std::fopen(utf8.c_str(), mode);
#endif
}

/** @brief True when `sig` (len bytes) appears at offset `at`. */
bool has_prefix(const uint8_t* d, std::size_t n, const char* sig, std::size_t len, std::size_t at = 0)
{
  return n >= at + len && std::memcmp(d + at, sig, len) == 0;
}

/** @brief Sniffs the signatures that identify a file by its first bytes alone. */
FileKind sniff_signature(const uint8_t* d, std::size_t n)
{
  if(has_prefix(d, n, "\xFF\xD8\xFF", 3))
  {
    return FileKind::JPEG;
  }
  if(has_prefix(d, n, "\x89PNG\r\n\x1a\n", 8))
  {
    return FileKind::PNG;
  }
  if(has_prefix(d, n, "GIF87a", kGifSignatureBytes) || has_prefix(d, n, "GIF89a", kGifSignatureBytes))
  {
    return FileKind::GIF;
  }
  if(has_prefix(d, n, "BM", 2) && n > kBmpHeaderBytes)
  {
    return FileKind::BMP;
  }
  if(has_prefix(d, n, "II*\0", 4) || has_prefix(d, n, "MM\0*", 4))
  {
    return FileKind::TIFF;
  }
  if(has_prefix(d, n, "RIFF", 4) && n >= kRiffHeaderBytes)
  {
    if(has_prefix(d, n, "WEBP", 4, kRiffTypeOffset))
    {
      return FileKind::WebP;
    }
    if(has_prefix(d, n, "AVI ", 4, kRiffTypeOffset))
    {
      return FileKind::Video;
    }
  }
  if(has_prefix(d, n, "\x1A\x45\xDF\xA3", 4) /* Matroska / WebM */ || has_prefix(d, n, "FLV", 3)
     || has_prefix(d, n, "\x00\x00\x01\xBA", 4) /* MPEG-PS */ || has_prefix(d, n, "OggS", 4)
     || has_prefix(d, n, "\x30\x26\xB2\x75", 4) /* ASF / WMV */)
  {
    return FileKind::Video;
  }
  return FileKind::Unknown;
}

/** @brief Classifies ISO base-media files (MP4, MOV, HEIF, AVIF, ...) by their major brand. */
FileKind sniff_iso_bmff(const uint8_t* d, std::size_t n)
{
  if(n < kBrandOffset + kBrandLength || !has_prefix(d, n, "ftyp", 4, kFtypOffset))
  {
    return FileKind::Unknown;
  }
  const std::string brand(reinterpret_cast<const char*>(d) + kBrandOffset, kBrandLength);
  if(brand == "avif" || brand == "avis")
  {
    return FileKind::AVIF;
  }
  for(const char* heif : kHeifBrands)
  {
    if(brand == heif)
    {
      return FileKind::HEIF;
    }
  }
  return FileKind::Video; /* isom, mp41, mp42, qt, M4V, avc1, dash, 3gp ... */
}

/** @brief True when the bytes look like the start of an HTML or XML document. */
bool looks_like_html(const uint8_t* d, std::size_t n)
{
  std::size_t i = has_prefix(d, n, "\xEF\xBB\xBF", kBomBytes) ? kBomBytes : 0;
  while(i < n && std::isspace(d[i]) != 0)
  {
    ++i;
  }
  std::string head;
  for(std::size_t k = i; k < n && k < i + kHtmlProbeChars; ++k)
  {
    head.push_back(static_cast<char>(std::tolower(d[k])));
  }
  return starts_with(head, "<!doctype") || starts_with(head, "<html") || starts_with(head, "<?xml")
         || starts_with(head, "<head");
}

} // namespace

Image Image::blank(int w, int h, uint8_t v)
{
  Image im;
  CC_REQUIRE(w > 0 && h > 0, return im);
  im.w = w;
  im.h = h;
  im.rgba.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * kImageChannels, v);
  return im;
}

const char* file_kind_name(FileKind k)
{
  switch(k)
  {
    case FileKind::JPEG: return "JPEG";
    case FileKind::PNG: return "PNG";
    case FileKind::WebP: return "WebP";
    case FileKind::GIF: return "GIF";
    case FileKind::BMP: return "BMP";
    case FileKind::AVIF: return "AVIF";
    case FileKind::HEIF: return "HEIF/HEIC";
    case FileKind::TIFF: return "TIFF";
    case FileKind::Video: return "video";
    case FileKind::HTML: return "web page";
    case FileKind::Other: return "other";
    case FileKind::Unknown: break;
  }
  return "unknown";
}

bool file_kind_is_image(FileKind k)
{
  switch(k)
  {
    case FileKind::JPEG:
    case FileKind::PNG:
    case FileKind::WebP:
    case FileKind::GIF:
    case FileKind::BMP:
    case FileKind::AVIF:
    case FileKind::HEIF:
    case FileKind::TIFF: return true;
    case FileKind::Video:
    case FileKind::HTML:
    case FileKind::Other:
    case FileKind::Unknown: break;
  }
  return false;
}

FileKind sniff_bytes(const uint8_t* d, std::size_t n, const std::string& hint_ext)
{
  if(d == nullptr || n < kSniffMinBytes)
  {
    return FileKind::Unknown;
  }
  FileKind kind = sniff_signature(d, n);
  if(kind == FileKind::Unknown)
  {
    kind = sniff_iso_bmff(d, n);
  }
  if(kind != FileKind::Unknown)
  {
    return kind;
  }
  if(looks_like_html(d, n))
  {
    return FileKind::HTML;
  }
  const std::string e = to_lower(hint_ext);
  for(const char* v : kVideoExtensions)
  {
    if(e == v)
    {
      return FileKind::Video;
    }
  }
  return FileKind::Unknown;
}

bool file_exists(const std::string& path)
{
  std::error_code ec;
  const bool regular = std::filesystem::is_regular_file(to_path(path), ec);
  return !ec && regular;
}

bool file_size(const std::string& path, uint64_t& size)
{
  std::error_code ec;
  const std::uintmax_t s = std::filesystem::file_size(to_path(path), ec);
  if(ec)
  {
    return false;
  }
  size = static_cast<uint64_t>(s);
  return true;
}

bool read_file(const std::string& path, std::vector<uint8_t>& out, std::string& err, uint64_t max_bytes)
{
  uint64_t size = 0;
  if(!file_size(path, size))
  {
    err = "cannot open '" + path + "'";
    return false;
  }
  if(max_bytes != 0 && size > max_bytes)
  {
    err = "file is " + format_bytes(size) + ", above the limit of " + format_bytes(max_bytes);
    return false;
  }
  if(size > static_cast<uint64_t>(SIZE_MAX))
  {
    err = "file is too large for this platform";
    return false;
  }
  FILE* f = open_file(path, "rb");
  if(f == nullptr)
  {
    err = "cannot open '" + path + "'";
    return false;
  }
  out.resize(static_cast<std::size_t>(size));
  const std::size_t got = size != 0 ? std::fread(out.data(), 1, static_cast<std::size_t>(size), f) : 0;
  const int close_result = std::fclose(f);
  CC_ENSURE(close_result == 0);
  if(got != size)
  {
    err = "short read on '" + path + "'";
    return false;
  }
  return true;
}

bool read_file_head(const std::string& path, std::size_t n, std::vector<uint8_t>& out, std::string& err)
{
  FILE* f = open_file(path, "rb");
  if(f == nullptr)
  {
    err = "cannot open '" + path + "'";
    return false;
  }
  out.resize(n);
  out.resize(n != 0 ? std::fread(out.data(), 1, n, f) : 0);
  const int close_result = std::fclose(f);
  CC_ENSURE(close_result == 0);
  return true;
}

bool write_file(const std::string& path, const uint8_t* data, std::size_t n, std::string& err)
{
  CC_REQUIRE(data != nullptr || n == 0, err = "internal: null data"; return false);
  FILE* f = open_file(path, "wb");
  if(f == nullptr)
  {
    err = "cannot write '" + path + "'";
    return false;
  }
  const std::size_t put = n != 0 ? std::fwrite(data, 1, n, f) : 0;
  const bool closed = std::fclose(f) == 0;
  if(put != n || !closed)
  {
    err = "short write on '" + path + "'";
    return false;
  }
  return true;
}

/* ---- decoders ---------------------------------------------------------- */

namespace {

/** @brief Offsets and sizes of the header fields read by header_size(). */
constexpr std::size_t kPngWidthAt = 16;
constexpr std::size_t kPngHeaderBytes = 24;
constexpr std::size_t kGifWidthAt = 6;
constexpr std::size_t kGifHeaderBytes = 10;
constexpr std::size_t kBmpInfoSizeAt = 14;
constexpr std::size_t kBmpCoreHeaderSize = 12;
constexpr std::size_t kBmpInfoWidthAt = 18;
constexpr std::size_t kBmpInfoBytes = 26;
constexpr std::size_t kWebpChunkAt = 12;
constexpr std::size_t kWebpVp8DimsAt = 26;
constexpr std::size_t kWebpVp8lBitsAt = 21;
constexpr std::size_t kWebpVp8xDimsAt = 24;
constexpr std::size_t kWebpHeaderBytes = 30;
constexpr unsigned kWebpVp8DimMask = 0x3FFF;
constexpr unsigned kWebpVp8lDimBits = 14;
constexpr unsigned kWebpVp8lDimMask = (1U << kWebpVp8lDimBits) - 1U;
/** @brief 16-bit to 8-bit sample scale (65535 / 255). */
constexpr double kSixteenToEight = 1.0 / 257.0;
constexpr double kFloatToEight = 255.0;

/** @brief Shift of the most significant byte of a 32-bit value. */
constexpr unsigned kTopByteShift = 24;

uint32_t be32(const uint8_t* p)
{
  return (static_cast<uint32_t>(p[0]) << kTopByteShift) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
}
uint32_t le32(const uint8_t* p)
{
  return (static_cast<uint32_t>(p[3]) << kTopByteShift) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[0];
}
uint32_t le24(const uint8_t* p) { return (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[0]; }
uint32_t le16(const uint8_t* p) { return (static_cast<uint32_t>(p[1]) << 8) | p[0]; }

/** @brief Clamps a header value to int (huge values then fail the edge check). */
int to_dim(uint32_t v) { return v > static_cast<uint32_t>(INT_MAX) ? INT_MAX : static_cast<int>(v); }

/** @brief Width and height of a WebP from its first chunk. */
bool webp_size(const uint8_t* d, std::size_t n, int& w, int& h)
{
  if(n < kWebpHeaderBytes)
  {
    return false;
  }
  const uint8_t* chunk = d + kWebpChunkAt;
  if(std::memcmp(chunk, "VP8 ", 4) == 0)
  {
    w = to_dim(le16(d + kWebpVp8DimsAt) & kWebpVp8DimMask);
    h = to_dim(le16(d + kWebpVp8DimsAt + 2) & kWebpVp8DimMask);
    return true;
  }
  if(std::memcmp(chunk, "VP8L", 4) == 0)
  {
    const uint32_t bits = le32(d + kWebpVp8lBitsAt);
    w = to_dim((bits & kWebpVp8lDimMask) + 1U);
    h = to_dim(((bits >> kWebpVp8lDimBits) & kWebpVp8lDimMask) + 1U);
    return true;
  }
  if(std::memcmp(chunk, "VP8X", 4) == 0)
  {
    w = to_dim(le24(d + kWebpVp8xDimsAt) + 1U);
    h = to_dim(le24(d + kWebpVp8xDimsAt + 3) + 1U);
    return true;
  }
  return false;
}

/**
 * @brief Image size from the header, for the formats where that is cheap,
 * so that oversized files are refused before anything is allocated.
 * @return false when the size is not known before decoding (the decoded size is checked instead).
 */
bool header_size(FileKind kind, const uint8_t* d, std::size_t n, int& w, int& h)
{
  switch(kind)
  {
    case FileKind::JPEG:
    {
      const JpegInfo ji = parse_jpeg_info(d, n);
      w = ji.width;
      h = ji.height;
      return ji.ok;
    }
    case FileKind::PNG:
      if(n < kPngHeaderBytes)
      {
        return false;
      }
      w = to_dim(be32(d + kPngWidthAt));
      h = to_dim(be32(d + kPngWidthAt + 4));
      return true;
    case FileKind::GIF:
      if(n < kGifHeaderBytes)
      {
        return false;
      }
      w = to_dim(le16(d + kGifWidthAt));
      h = to_dim(le16(d + kGifWidthAt + 2));
      return true;
    case FileKind::BMP:
    {
      if(n < kBmpInfoBytes)
      {
        return false;
      }
      if(le32(d + kBmpInfoSizeAt) == kBmpCoreHeaderSize)
      {
        w = to_dim(le16(d + kBmpInfoWidthAt));
        h = to_dim(le16(d + kBmpInfoWidthAt + 2));
        return true;
      }
      w = to_dim(le32(d + kBmpInfoWidthAt));
      const int32_t sh = static_cast<int32_t>(le32(d + kBmpInfoWidthAt + 4)); /* negative = top-down */
      h = sh == INT32_MIN ? INT_MAX : std::abs(sh);
      return true;
    }
    case FileKind::WebP: return webp_size(d, n, w, h);
    default: return false;
  }
}

/** @brief Rejects images larger than the configured edge (also guards the size arithmetic). */
bool check_edge(int w, int h, int max_edge_px, std::string& err)
{
  if(w <= 0 || h <= 0)
  {
    err = "empty image";
    return false;
  }
  if(w > max_edge_px || h > max_edge_px)
  {
    err = "image is " + std::to_string(w) + "x" + std::to_string(h) + ", above the limit of " + std::to_string(max_edge_px)
          + " px per edge (limits.max_image_edge_px)";
    return false;
  }
  return true;
}

/** @brief Converts whatever imdecode returned (1/3/4 channels, 8/16-bit or float, BGR order) to 8-bit RGBA. */
bool mat_to_rgba(const cv::Mat& m, Image& out, std::string& err)
{
  cv::Mat m8;
  switch(m.depth())
  {
    case CV_8U: m8 = m; break;
    case CV_16U: m.convertTo(m8, CV_8U, kSixteenToEight); break;
    case CV_32F:
    case CV_64F: m.convertTo(m8, CV_8U, kFloatToEight); break;
    default: err = "unsupported sample format"; return false;
  }
  Image img = Image::blank(m8.cols, m8.rows);
  cv::Mat dst(img.h, img.w, CV_8UC4, img.rgba.data());
  switch(m8.channels())
  {
    case 1: cv::cvtColor(m8, dst, cv::COLOR_GRAY2RGBA); break;
    case 3: cv::cvtColor(m8, dst, cv::COLOR_BGR2RGBA); break;
    case 4: cv::cvtColor(m8, dst, cv::COLOR_BGRA2RGBA); break;
    default: err = "unsupported channel count " + std::to_string(m8.channels()); return false;
  }
  out = std::move(img);
  return true;
}

/**
 * @brief Decodes with OpenCV's imgcodecs (libjpeg-turbo, libpng, libwebp, libtiff, GIF, BMP).
 * JPEGs are decoded with IMREAD_COLOR so that the EXIF orientation is applied, as a site does.
 */
bool decode_opencv(const uint8_t* data, std::size_t n, FileKind kind, int max_edge_px, Image& out, std::string& err)
{
  if(n > static_cast<std::size_t>(INT_MAX))
  {
    err = "file is too large to decode";
    return false;
  }
  const cv::Mat buf(1, static_cast<int>(n), CV_8UC1, const_cast<uint8_t*>(data));
  const int flags = kind == FileKind::JPEG ? cv::IMREAD_COLOR : (cv::IMREAD_UNCHANGED | cv::IMREAD_ANYDEPTH);
  cv::Mat m;
  try
  {
    m = cv::imdecode(buf, flags);
  }
  catch(const cv::Exception& e)
  {
    err = std::string("OpenCV: ") + e.what();
    return false;
  }
  if(m.empty())
  {
    err = std::string("OpenCV could not decode this ") + file_kind_name(kind) + " file";
    return false;
  }
  if(!check_edge(m.cols, m.rows, max_edge_px, err))
  {
    return false;
  }
  return mat_to_rgba(m, out, err);
}

/** @brief Fills in the result for data that is not an image at all. */
bool explain_not_an_image(FileKind kind, LoadResult& r)
{
  if(kind == FileKind::Video)
  {
    r.error = "this is a video file";
    return true;
  }
  if(kind == FileKind::HTML)
  {
    r.error = "the data is a web page, not an image (use the direct image link, e.g. right-click > 'Copy image address')";
    return true;
  }
  return false;
}

/** @brief True for kinds OpenCV's imgcodecs are built without (they go to ImageIO / ffmpeg). */
bool opencv_lacks(FileKind kind) { return kind == FileKind::AVIF || kind == FileKind::HEIF; }

} // namespace

LoadResult decode_image(const uint8_t* data, std::size_t n, int max_edge_px, Image& out, const std::string& hint_ext)
{
  LoadResult r;
  CC_REQUIRE(data != nullptr && n > 0, r.error = "empty data"; return r);
  CC_REQUIRE(max_edge_px > 0, r.error = "internal: bad edge limit"; return r);
  r.kind = sniff_bytes(data, n, hint_ext);
  if(explain_not_an_image(r.kind, r))
  {
    return r;
  }
  int hw = 0;
  int hh = 0;
  if(header_size(r.kind, data, n, hw, hh) && !check_edge(hw, hh, max_edge_px, r.error))
  {
    return r;
  }
  if(!opencv_lacks(r.kind))
  {
    r.ok = decode_opencv(data, n, r.kind, max_edge_px, out, r.error);
    r.decoder = "OpenCV imgcodecs";
  }
#ifdef __APPLE__
  if(!r.ok)
  {
    std::string err;
    if(decode_with_imageio(data, n, max_edge_px, out, err))
    {
      r.ok = true;
      r.error.clear();
      r.decoder = "macOS ImageIO";
    }
    else
    {
      r.error = r.error.empty() ? err : r.error + "; ImageIO: " + err;
    }
  }
#endif
  if(!r.ok && opencv_lacks(r.kind) && r.error.empty())
  {
    r.error = std::string(file_kind_name(r.kind)) + " decoding needs ffmpeg (install it and the app will use it as a fallback)";
  }
  if(r.ok && r.kind == FileKind::Unknown)
  {
    r.kind = FileKind::Other;
  }
  return r;
}

LoadResult load_image_file(const std::string& path, const LimitsConfig& limits, Image& out)
{
  std::vector<uint8_t> bytes;
  std::string err;
  if(!read_file(path, bytes, err, limits.max_image_bytes))
  {
    LoadResult r;
    r.error = err;
    return r;
  }
  if(bytes.empty())
  {
    LoadResult r;
    r.error = "file is empty";
    return r;
  }
  return decode_image(bytes.data(), bytes.size(), limits.max_image_edge_px, out, file_extension(path));
}

/* ---- geometry ---------------------------------------------------------- */

Image resample(const Image& src, int w, int h)
{
  if(!src.valid() || w <= 0 || h <= 0)
  {
    return Image();
  }
  if(src.w == w && src.h == h)
  {
    return src;
  }
  /* sRGB-aware: resized in linear light with premultiplied alpha.  INTER_AREA (a
   * proper box pre-filter) when shrinking in both directions, bicubic otherwise. */
  const std::size_t src_px = static_cast<std::size_t>(src.w) * static_cast<std::size_t>(src.h);
  const std::size_t dst_px = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
  use_gpu_for(std::max(src_px, dst_px));
  const cv::UMat in = cvops::upload(src);
  cv::UMat lin;
  cvops::to_linear_premul(in, lin);
  const int interp = (w <= src.w && h <= src.h) ? cv::INTER_AREA : cv::INTER_CUBIC;
  cv::UMat scaled;
  cv::resize(lin, scaled, cv::Size(w, h), 0.0, 0.0, interp);
  cv::UMat out;
  cvops::from_linear_premul(scaled, out);
  Image res = cvops::download(out);
  CC_ENSURE(res.valid());
  return res;
}

Image crop(const Image& src, int x, int y, int w, int h)
{
  if(!src.valid())
  {
    return Image();
  }
  const int cx = std::clamp(x, 0, std::max(0, src.w - 1));
  const int cy = std::clamp(y, 0, std::max(0, src.h - 1));
  const int cw = std::clamp(w, 1, src.w - cx);
  const int ch = std::clamp(h, 1, src.h - cy);
  Image out = Image::blank(cw, ch);
  for(int r = 0; r < ch; ++r)
  {
    std::memcpy(out.px(0, r), src.px(cx, cy + r), static_cast<std::size_t>(cw) * kImageChannels);
  }
  return out;
}

Image fit_cover(const Image& src, int w, int h, int offset_x, int offset_y)
{
  if(!src.valid() || w <= 0 || h <= 0)
  {
    return Image();
  }
  const double sx = static_cast<double>(w) / src.w;
  const double sy = static_cast<double>(h) / src.h;
  const double s = std::max(sx, sy); /* cover */
  const int rw = std::max(w, static_cast<int>(std::lround(src.w * s)));
  const int rh = std::max(h, static_cast<int>(std::lround(src.h * s)));
  const Image scaled = resample(src, rw, rh);
  if(!scaled.valid())
  {
    return Image();
  }
  const int cx = std::clamp((rw - w) / 2 + offset_x, 0, rw - w);
  const int cy = std::clamp((rh - h) / 2 + offset_y, 0, rh - h);
  return crop(scaled, cx, cy, w, h);
}

bool save_png(const Image& img, const std::string& path, std::string& err)
{
  if(!img.valid())
  {
    err = "empty image";
    return false;
  }
  const cv::Mat rgba(img.h, img.w, CV_8UC4, const_cast<uint8_t*>(img.rgba.data()));
  cv::Mat bgra;
  cv::cvtColor(rgba, bgra, cv::COLOR_RGBA2BGRA);
  std::vector<uint8_t> png;
  bool encoded = false;
  try
  {
    encoded = cv::imencode(".png", bgra, png);
  }
  catch(const cv::Exception& e)
  {
    err = std::string("PNG encode failed: ") + e.what();
    return false;
  }
  if(!encoded || png.empty())
  {
    err = "PNG encode failed";
    return false;
  }
  return write_file(path, png.data(), png.size(), err);
}

} // namespace cc
