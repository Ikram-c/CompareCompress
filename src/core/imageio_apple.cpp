/**
 * @file imageio_apple.cpp
 * @brief ImageIO decoder (see imageio_apple.h).  Compiled on Apple platforms only.
 */
#include "core/imageio_apple.h"

#include "util/contract.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <algorithm>
#include <climits>
#include <string>
#include <utility>

namespace cc {

namespace {

constexpr int kLevelMax = 255;
constexpr std::size_t kBitsPerComponent = 8;
constexpr int kThumbnailOptionCount = 4;
constexpr std::size_t kChannels = static_cast<std::size_t>(kImageChannels);

/** @brief Releases a CoreFoundation object when the scope ends. */
template <typename T>
class CfRef
{
public:
  explicit CfRef(T ref) : ref_(ref) {}
  ~CfRef()
  {
    if(ref_ != nullptr)
    {
      CFRelease(ref_);
    }
  }
  CfRef(const CfRef&) = delete;
  CfRef& operator=(const CfRef&) = delete;
  [[nodiscard]] T get() const { return ref_; }

private:
  T ref_;
};

/** @brief Reads an integer entry of a properties dictionary. */
int dict_int(CFDictionaryRef dict, CFStringRef key)
{
  const void* v = CFDictionaryGetValue(dict, key);
  if(v == nullptr || CFGetTypeID(v) != CFNumberGetTypeID())
  {
    return 0;
  }
  int out = 0;
  if(!CFNumberGetValue(static_cast<CFNumberRef>(v), kCFNumberIntType, &out))
  {
    return 0;
  }
  return out;
}

bool edge_ok(long w, long h, int max_edge_px, std::string& err)
{
  if(w <= 0 || h <= 0)
  {
    err = "ImageIO reported an empty image";
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

/** @brief Converts premultiplied RGBA (what CoreGraphics draws) to straight alpha in place. */
void unpremultiply(Image& img)
{
  const std::size_t n = img.rgba.size();
  for(std::size_t i = 0; i + 3 < n; i += kChannels)
  {
    const int a = img.rgba[i + 3];
    if(a == 0 || a == kLevelMax)
    {
      continue;
    }
    for(std::size_t c = 0; c < 3; ++c)
    {
      const int v = (img.rgba[i + c] * kLevelMax + a / 2) / a;
      img.rgba[i + c] = static_cast<uint8_t>(std::min(v, kLevelMax));
    }
  }
}

/** @brief Pixel size from the file header (before decoding); false when it is missing. */
bool header_size(CGImageSourceRef src, int& w, int& h)
{
  const CfRef<CFDictionaryRef> props(CGImageSourceCopyPropertiesAtIndex(src, 0, nullptr));
  if(props.get() == nullptr)
  {
    return false;
  }
  w = dict_int(props.get(), kCGImagePropertyPixelWidth);
  h = dict_int(props.get(), kCGImagePropertyPixelHeight);
  return w > 0 && h > 0;
}

/**
 * @brief Full-size decode with the EXIF/HEIF orientation applied.  The
 * thumbnail API is the one that honours the orientation; asking for a
 * "thumbnail" as large as the image gives the whole image.
 */
CGImageRef create_oriented_image(CGImageSourceRef src, int max_px)
{
  const CfRef<CFNumberRef> max_num(CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &max_px));
  const void* keys[kThumbnailOptionCount] = {kCGImageSourceCreateThumbnailFromImageAlways, kCGImageSourceCreateThumbnailWithTransform,
                                             kCGImageSourceThumbnailMaxPixelSize, kCGImageSourceShouldCacheImmediately};
  const void* values[kThumbnailOptionCount] = {kCFBooleanTrue, kCFBooleanTrue, max_num.get(), kCFBooleanTrue};
  const CfRef<CFDictionaryRef> opts(CFDictionaryCreate(kCFAllocatorDefault, keys, values, kThumbnailOptionCount,
                                                       &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
  return CGImageSourceCreateThumbnailAtIndex(src, 0, opts.get());
}

/** @brief Draws a CGImage into 8-bit sRGB RGBA (straight alpha). */
bool draw_to_rgba(CGImageRef img, int max_edge_px, Image& out, std::string& err)
{
  const std::size_t w = CGImageGetWidth(img);
  const std::size_t h = CGImageGetHeight(img);
  if(w > static_cast<std::size_t>(INT_MAX) || h > static_cast<std::size_t>(INT_MAX))
  {
    err = "image is too large";
    return false;
  }
  if(!edge_ok(static_cast<long>(w), static_cast<long>(h), max_edge_px, err))
  {
    return false;
  }
  Image res = Image::blank(static_cast<int>(w), static_cast<int>(h));
  const CfRef<CGColorSpaceRef> srgb(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
  const CfRef<CGContextRef> ctx(CGBitmapContextCreate(res.rgba.data(), w, h, kBitsPerComponent, w * kChannels, srgb.get(),
                                                      static_cast<uint32_t>(kCGImageAlphaPremultipliedLast)
                                                          | static_cast<uint32_t>(kCGBitmapByteOrder32Big)));
  if(ctx.get() == nullptr)
  {
    err = "could not create a drawing context";
    return false;
  }
  CGContextSetBlendMode(ctx.get(), kCGBlendModeCopy);
  CGContextSetInterpolationQuality(ctx.get(), kCGInterpolationNone);
  CGContextDrawImage(ctx.get(), CGRectMake(0, 0, static_cast<CGFloat>(w), static_cast<CGFloat>(h)), img);
  unpremultiply(res);
  out = std::move(res);
  return true;
}

} // namespace

bool decode_with_imageio(const uint8_t* data, std::size_t n, int max_edge_px, Image& out, std::string& err)
{
  CC_REQUIRE(data != nullptr && n > 0 && max_edge_px > 0, err = "internal: bad arguments"; return false);
  if(n > static_cast<std::size_t>(LONG_MAX))
  {
    err = "file is too large";
    return false;
  }
  const CfRef<CFDataRef> bytes(CFDataCreate(kCFAllocatorDefault, data, static_cast<CFIndex>(n)));
  if(bytes.get() == nullptr)
  {
    err = "out of memory";
    return false;
  }
  const CfRef<CGImageSourceRef> src(CGImageSourceCreateWithData(bytes.get(), nullptr));
  if(src.get() == nullptr || CGImageSourceGetCount(src.get()) < 1)
  {
    err = "ImageIO does not recognise this file";
    return false;
  }
  int pw = 0;
  int ph = 0;
  if(!header_size(src.get(), pw, ph) || !edge_ok(pw, ph, max_edge_px, err))
  {
    if(err.empty())
    {
      err = "ImageIO reported no image size";
    }
    return false;
  }
  const CfRef<CGImageRef> img(create_oriented_image(src.get(), std::max(pw, ph)));
  if(img.get() == nullptr)
  {
    err = "ImageIO could not decode the image";
    return false;
  }
  return draw_to_rgba(img.get(), max_edge_px, out, err);
}

} // namespace cc
