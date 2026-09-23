/**
 * @file test_jpeg_info.cpp
 * @brief JPEG header parsing, quality estimation, sniffing and resampling.
 *
 * OpenCV's JPEG encoder (libjpeg-turbo) uses the IJG Annex-K tables and the
 * exact IJG quality scaling, so the estimator must recover the quality exactly.
 */
#include "core/config.h"
#include "core/image.h"
#include "core/jpeg_info.h"
#include "test_util.h"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <cstdlib>
#include <cstring>
#include <vector>

using namespace cc;

namespace {

/** @brief A 96x64 test JPEG at the given IJG quality (4:2:0 up to q90, 4:4:4 above). */
std::vector<uint8_t> make_jpeg(int quality)
{
  const int W = 96;
  const int H = 64;
  cv::Mat bgr(H, W, CV_8UC3);
  for(int y = 0; y < H; ++y)
  {
    for(int x = 0; x < W; ++x)
    {
      cv::Vec3b& p = bgr.at<cv::Vec3b>(y, x);
      p[2] = static_cast<uint8_t>(x * 2);
      p[1] = static_cast<uint8_t>(y * 3);
      p[0] = static_cast<uint8_t>((x * y) & 0xFF);
    }
  }
  const int sampling = quality <= 90 ? cv::IMWRITE_JPEG_SAMPLING_FACTOR_420 : cv::IMWRITE_JPEG_SAMPLING_FACTOR_444;
  std::vector<uint8_t> out;
  cv::imencode(".jpg", bgr, out, {cv::IMWRITE_JPEG_QUALITY, quality, cv::IMWRITE_JPEG_SAMPLING_FACTOR, sampling});
  return out;
}

/** @brief Signature detection on synthetic headers. */
void check_sniffing()
{
  const uint8_t png_sig[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0};
  CHECK(!parse_jpeg_info(png_sig, sizeof png_sig).ok);
  CHECK(sniff_bytes(png_sig, sizeof png_sig) == FileKind::PNG);
  const uint8_t webp[] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P', 'V', 'P', '8', ' '};
  CHECK(sniff_bytes(webp, sizeof webp) == FileKind::WebP);
  const uint8_t mp4[] = {0, 0, 0, 0x18, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm', 0, 0, 0, 0};
  CHECK(sniff_bytes(mp4, sizeof mp4) == FileKind::Video);
  const uint8_t avif[] = {0, 0, 0, 0x1c, 'f', 't', 'y', 'p', 'a', 'v', 'i', 'f', 0, 0, 0, 0};
  CHECK(sniff_bytes(avif, sizeof avif) == FileKind::AVIF);
  const uint8_t heic[] = {0, 0, 0, 0x1c, 'f', 't', 'y', 'p', 'h', 'e', 'i', 'c', 0, 0, 0, 0};
  CHECK(sniff_bytes(heic, sizeof heic) == FileKind::HEIF);
  const uint8_t mkv[] = {0x1A, 0x45, 0xDF, 0xA3, 0, 0, 0, 0};
  CHECK(sniff_bytes(mkv, sizeof mkv) == FileKind::Video);
  const char* html = "  <!DOCTYPE html><html>";
  CHECK(sniff_bytes(reinterpret_cast<const uint8_t*>(html), std::strlen(html)) == FileKind::HTML);
  const uint8_t junk[] = {1, 2, 3, 4, 5, 6, 7, 8};
  CHECK(sniff_bytes(junk, sizeof junk, "mov") == FileKind::Video); /* extension hint */
  CHECK(sniff_bytes(junk, sizeof junk, "txt") == FileKind::Unknown);
  CHECK(sniff_bytes(nullptr, 0) == FileKind::Unknown);
}

/** @brief Resampling keeps size and roughly preserves a flat colour. */
void check_resampling()
{
  const Image flat = Image::blank(40, 30, 200);
  const Image small = resample(flat, 20, 15);
  CHECK(small.w == 20 && small.h == 15);
  CHECK(std::abs(static_cast<int>(small.px(5, 5)[0]) - 200) <= 1);
  const Image cover = fit_cover(flat, 10, 10);
  CHECK(cover.w == 10 && cover.h == 10);
  CHECK(!resample(flat, 0, 10).valid());
  CHECK(!Image::blank(0, 5).valid());
}

} // namespace

int main()
{
  const Config& cfg = default_config();
  for(const int q : {10, 35, 50, 75, 85, 92, 100})
  {
    const std::vector<uint8_t> jpg = make_jpeg(q);
    CHECK(sniff_bytes(jpg.data(), jpg.size()) == FileKind::JPEG);
    const JpegInfo info = parse_jpeg_info(jpg.data(), jpg.size());
    CHECK(info.ok);
    CHECK(info.width == 96 && info.height == 64);
    CHECK(info.components == 3);
    CHECK(!info.progressive);
    CHECK(info.quant_tables >= 2);
    CHECK(info.quality == q);
    CHECK(info.table_fit_error < 0.5f);
    /* make_jpeg() subsamples chroma (4:2:0) up to quality 90 and uses 4:4:4 above */
    CHECK_EQ_STR(info.subsampling, q <= 90 ? "4:2:0" : "4:4:4");
    CHECK(info.scans == 1);
    CHECK(info.has_jfif);
    CHECK(info.summary(cfg.metrics.custom_table_fit_error_pct).find("baseline JPEG") == 0);

    /* the decoder round-trips through the image loader */
    Image im;
    const LoadResult r = decode_image(jpg.data(), jpg.size(), cfg.limits.max_image_edge_px, im);
    CHECK(r.ok);
    CHECK(im.w == 96 && im.h == 64);
    CHECK(im.px(0, 0)[3] == kOpaqueAlpha);

    /* the edge limit is enforced by every decoder */
    Image refused;
    const LoadResult limited = decode_image(jpg.data(), jpg.size(), 32, refused);
    CHECK(!limited.ok);
    CHECK(!limited.error.empty());
  }
  check_sniffing();
  check_resampling();
  return test_summary("test_jpeg_info");
}
