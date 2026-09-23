/**
 * @file cv_ops.cpp
 * @brief OpenCL launches of the custom kernels and their CPU twins.
 */
#include "core/cv_ops.h"

#include "core/cl_kernels.h"
#include "core/metrics.h"
#include "util/contract.h"

#include <opencv2/core/ocl.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>

namespace cc::cvops {

namespace {

constexpr float kMaxLevel = 255.0f;
constexpr float kRgbChannels = 3.0f;
constexpr float kSrgbLinearCutoff = 0.0031308f;
constexpr float kSrgbLinearSlope = 12.92f;
constexpr float kSrgbScale = 1.055f;
constexpr float kSrgbOffset = 0.055f;
constexpr float kSrgbInvGamma = 1.0f / 2.4f;
constexpr int kLutEntries = 256;

/** @brief Byte offset of pixel `x` in an 8-bit RGBA row, or of element `x` in a 4-channel float row. */
std::ptrdiff_t px_off(int x) { return static_cast<std::ptrdiff_t>(x) * kImageChannels; }

/** @brief Size of the synthetic self-test pattern (small: it runs at start-up). */
constexpr int kSelfTestW = 67;
constexpr int kSelfTestH = 41;
/** @brief SSIM stabilising constants of the self-test ((0.01 * 255)^2, (0.03 * 255)^2). */
constexpr float kSelfTestC1 = 6.5025f;
constexpr float kSelfTestC2 = 58.5225f;

std::atomic<bool> g_kernels_ready{false};
std::atomic<unsigned long> g_gpu_launches{0};

const cv::ocl::ProgramSource& program()
{
  static const cv::ocl::ProgramSource src(kClKernelSource);
  return src;
}

/** @brief Kernel handle for the current default context; empty when the build failed. */
cv::ocl::Kernel make_kernel(const char* name)
{
  cv::String log;
  cv::ocl::Kernel k(name, program(), cv::String(), &log);
  return k;
}

/** @brief True when this call should try the OpenCL path. */
bool want_gpu() { return g_kernels_ready.load() && cv::ocl::useOpenCL(); }

/** @brief The sRGB-to-linear table on the device (1 x 256 CV_32F). */
cv::UMat device_lut()
{
  cv::UMat lut;
  const cv::Mat host(1, kLutEntries, CV_32F, const_cast<float*>(srgb_to_linear_table()));
  host.copyTo(lut);
  return lut;
}

bool run2d(cv::ocl::Kernel& k, int cols, int rows)
{
  std::size_t global[2] = {static_cast<std::size_t>(cols), static_cast<std::size_t>(rows)};
  const bool ok = k.run(2, global, nullptr, false);
  if(ok)
  {
    ++g_gpu_launches;
  }
  return ok;
}

/* ---- CPU twins ---------------------------------------------------------- */

int max_channel_difference(const uint8_t* p, const uint8_t* q)
{
  const int dr = std::abs(p[0] - q[0]);
  const int dg = std::abs(p[1] - q[1]);
  const int db = std::abs(p[2] - q[2]);
  return std::max(dr, std::max(dg, db));
}

float squared_error(const uint8_t* p, const uint8_t* q)
{
  const float dr = static_cast<float>(p[0] - q[0]);
  const float dg = static_cast<float>(p[1] - q[1]);
  const float db = static_cast<float>(p[2] - q[2]);
  return (dr * dr + dg * dg + db * db) / kRgbChannels;
}

float cpu_metric(Metric m, const uint8_t* p, const uint8_t* q)
{
  switch(m)
  {
    case Metric::AbsDiff: return static_cast<float>(max_channel_difference(p, q));
    case Metric::LumaDiff: return std::fabs(luma601(p[0], p[1], p[2]) - luma601(q[0], q[1], q[2]));
    case Metric::DeltaE76: return delta_e76(srgb8_to_lab(p[0], p[1], p[2]), srgb8_to_lab(q[0], q[1], q[2]));
    case Metric::DeltaE2000: return delta_e2000(srgb8_to_lab(p[0], p[1], p[2]), srgb8_to_lab(q[0], q[1], q[2]));
    case Metric::SquaredError: return squared_error(p, q);
    case Metric::SSIM:
    case Metric::COUNT: break;
  }
  return 0.0f;
}

float linear_to_srgb(float c)
{
  c = std::clamp(c, 0.0f, 1.0f);
  return c <= kSrgbLinearCutoff ? kSrgbLinearSlope * c : kSrgbScale * std::pow(c, kSrgbInvGamma) - kSrgbOffset;
}

uint8_t to_level(float c) { return static_cast<uint8_t>(std::clamp(std::floor(c * kMaxLevel + 0.5f), 0.0f, kMaxLevel)); }

/** @brief Runs `row(y)` for every row through OpenCV's thread pool. */
template <typename F>
void for_rows(int rows, const F& row)
{
  cv::parallel_for_(cv::Range(0, rows), [&](const cv::Range& r) {
    for(int y = r.start; y < r.end; ++y)
    {
      row(y);
    }
  });
}

/** @brief Enum value the kernel uses for a metric. */
int metric_id(Metric m) { return static_cast<int>(m); }

/**
 * @brief Number of samples where two maps differ by more than `tol`
 * (8-bit maps are compared in units of 1/255); -1 when they are not comparable.
 */
long count_mismatches(const cv::UMat& a, const cv::UMat& b, float tol)
{
  if(a.size() != b.size() || a.type() != b.type())
  {
    return -1;
  }
  const double scale = a.depth() == CV_8U ? 1.0 / static_cast<double>(kMaxLevel) : 1.0;
  cv::Mat fa;
  cv::Mat fb;
  a.getMat(cv::ACCESS_READ).convertTo(fa, CV_32F, scale);
  b.getMat(cv::ACCESS_READ).convertTo(fb, CV_32F, scale);
  cv::Mat diff = cv::abs(fa - fb);
  diff = diff.reshape(1);
  cv::Mat bad = (diff > static_cast<double>(tol)) | (diff != diff); /* NaN counts as a mismatch */
  return static_cast<long>(cv::countNonZero(bad));
}

} // namespace

/* ---- transfers ---------------------------------------------------------- */

cv::UMat upload(const Image& img)
{
  cv::UMat u;
  CC_REQUIRE(img.valid(), return u);
  const cv::Mat host(img.h, img.w, CV_8UC4, const_cast<uint8_t*>(img.rgba.data()));
  host.copyTo(u);
  return u;
}

Image download(const cv::UMat& m)
{
  Image out;
  CC_REQUIRE(!m.empty() && m.type() == CV_8UC4, return out);
  out = Image::blank(m.cols, m.rows);
  cv::Mat dst(m.rows, m.cols, CV_8UC4, out.rgba.data());
  m.copyTo(dst);
  return out;
}

void download(const cv::UMat& m, std::vector<float>& out)
{
  out.clear();
  CC_REQUIRE(!m.empty() && m.type() == CV_32FC1, return);
  out.resize(static_cast<std::size_t>(m.rows) * static_cast<std::size_t>(m.cols));
  cv::Mat dst(m.rows, m.cols, CV_32FC1, out.data());
  m.copyTo(dst);
}

/* ---- operations --------------------------------------------------------- */

void metric_map(const cv::UMat& a, const cv::UMat& b, Metric m, cv::UMat& out)
{
  CC_REQUIRE(a.type() == CV_8UC4 && b.type() == CV_8UC4 && a.size() == b.size() && m != Metric::SSIM, return);
  out.create(a.size(), CV_32FC1);
  if(want_gpu())
  {
    cv::ocl::Kernel k = make_kernel("pixel_metric");
    const cv::UMat lut = device_lut();
    if(!k.empty())
    {
      k.args(cv::ocl::KernelArg::ReadOnly(a), cv::ocl::KernelArg::ReadOnlyNoSize(b), cv::ocl::KernelArg::WriteOnlyNoSize(out),
             cv::ocl::KernelArg::PtrReadOnly(lut), metric_id(m));
      if(run2d(k, a.cols, a.rows))
      {
        return;
      }
    }
  }
  const cv::Mat am = a.getMat(cv::ACCESS_READ);
  const cv::Mat bm = b.getMat(cv::ACCESS_READ);
  cv::Mat om = out.getMat(cv::ACCESS_WRITE);
  for_rows(am.rows, [&](int y) {
    const uint8_t* p = am.ptr<uint8_t>(y);
    const uint8_t* q = bm.ptr<uint8_t>(y);
    float* o = om.ptr<float>(y);
    for(int x = 0; x < am.cols; ++x)
    {
      o[x] = cpu_metric(m, p + px_off(x), q + px_off(x));
    }
  });
}

void pair_maps(const cv::UMat& a, const cv::UMat& b, cv::UMat& abs_map, cv::UMat& sq_map, cv::UMat& de_map)
{
  CC_REQUIRE(a.type() == CV_8UC4 && b.type() == CV_8UC4 && a.size() == b.size(), return);
  abs_map.create(a.size(), CV_32FC1);
  sq_map.create(a.size(), CV_32FC1);
  de_map.create(a.size(), CV_32FC1);
  if(want_gpu())
  {
    cv::ocl::Kernel k = make_kernel("pair_maps");
    const cv::UMat lut = device_lut();
    if(!k.empty())
    {
      k.args(cv::ocl::KernelArg::ReadOnly(a), cv::ocl::KernelArg::ReadOnlyNoSize(b), cv::ocl::KernelArg::WriteOnlyNoSize(abs_map),
             cv::ocl::KernelArg::WriteOnlyNoSize(sq_map), cv::ocl::KernelArg::WriteOnlyNoSize(de_map),
             cv::ocl::KernelArg::PtrReadOnly(lut));
      if(run2d(k, a.cols, a.rows))
      {
        return;
      }
    }
  }
  const cv::Mat am = a.getMat(cv::ACCESS_READ);
  const cv::Mat bm = b.getMat(cv::ACCESS_READ);
  cv::Mat ab = abs_map.getMat(cv::ACCESS_WRITE);
  cv::Mat sq = sq_map.getMat(cv::ACCESS_WRITE);
  cv::Mat de = de_map.getMat(cv::ACCESS_WRITE);
  for_rows(am.rows, [&](int y) {
    const uint8_t* p = am.ptr<uint8_t>(y);
    const uint8_t* q = bm.ptr<uint8_t>(y);
    float* o_abs = ab.ptr<float>(y);
    float* o_sq = sq.ptr<float>(y);
    float* o_de = de.ptr<float>(y);
    for(int x = 0; x < am.cols; ++x)
    {
      const uint8_t* pp = p + px_off(x);
      const uint8_t* qq = q + px_off(x);
      o_abs[x] = static_cast<float>(max_channel_difference(pp, qq));
      o_sq[x] = squared_error(pp, qq);
      o_de[x] = delta_e2000(srgb8_to_lab(pp[0], pp[1], pp[2]), srgb8_to_lab(qq[0], qq[1], qq[2]));
    }
  });
}

void luma_pair(const cv::UMat& a, const cv::UMat& b, cv::UMat& ya, cv::UMat& yb)
{
  CC_REQUIRE(a.type() == CV_8UC4 && b.type() == CV_8UC4 && a.size() == b.size(), return);
  ya.create(a.size(), CV_32FC1);
  yb.create(a.size(), CV_32FC1);
  if(want_gpu())
  {
    cv::ocl::Kernel k = make_kernel("luma_pair");
    if(!k.empty())
    {
      k.args(cv::ocl::KernelArg::ReadOnly(a), cv::ocl::KernelArg::ReadOnlyNoSize(b), cv::ocl::KernelArg::WriteOnlyNoSize(ya),
             cv::ocl::KernelArg::WriteOnlyNoSize(yb));
      if(run2d(k, a.cols, a.rows))
      {
        return;
      }
    }
  }
  const cv::Mat am = a.getMat(cv::ACCESS_READ);
  const cv::Mat bm = b.getMat(cv::ACCESS_READ);
  cv::Mat ym_a = ya.getMat(cv::ACCESS_WRITE);
  cv::Mat ym_b = yb.getMat(cv::ACCESS_WRITE);
  for_rows(am.rows, [&](int y) {
    const uint8_t* p = am.ptr<uint8_t>(y);
    const uint8_t* q = bm.ptr<uint8_t>(y);
    float* oa = ym_a.ptr<float>(y);
    float* ob = ym_b.ptr<float>(y);
    for(int x = 0; x < am.cols; ++x)
    {
      const uint8_t* pp = p + px_off(x);
      const uint8_t* qq = q + px_off(x);
      oa[x] = luma601(pp[0], pp[1], pp[2]);
      ob[x] = luma601(qq[0], qq[1], qq[2]);
    }
  });
}

void ssim_combine(const cv::UMat& mu_a, const cv::UMat& mu_b, const cv::UMat& e_aa, const cv::UMat& e_bb, const cv::UMat& e_ab,
                  float c1, float c2, cv::UMat& out)
{
  CC_REQUIRE(mu_a.type() == CV_32FC1 && mu_a.size() == mu_b.size() && mu_a.size() == e_aa.size() && mu_a.size() == e_bb.size()
                 && mu_a.size() == e_ab.size(),
             return);
  out.create(mu_a.size(), CV_32FC1);
  if(want_gpu())
  {
    cv::ocl::Kernel k = make_kernel("ssim_combine");
    if(!k.empty())
    {
      k.args(cv::ocl::KernelArg::ReadOnly(mu_a), cv::ocl::KernelArg::ReadOnlyNoSize(mu_b), cv::ocl::KernelArg::ReadOnlyNoSize(e_aa),
             cv::ocl::KernelArg::ReadOnlyNoSize(e_bb), cv::ocl::KernelArg::ReadOnlyNoSize(e_ab), cv::ocl::KernelArg::WriteOnlyNoSize(out),
             c1, c2);
      if(run2d(k, mu_a.cols, mu_a.rows))
      {
        return;
      }
    }
  }
  const cv::Mat ma = mu_a.getMat(cv::ACCESS_READ);
  const cv::Mat mb = mu_b.getMat(cv::ACCESS_READ);
  const cv::Mat aa = e_aa.getMat(cv::ACCESS_READ);
  const cv::Mat bb = e_bb.getMat(cv::ACCESS_READ);
  const cv::Mat ab = e_ab.getMat(cv::ACCESS_READ);
  cv::Mat om = out.getMat(cv::ACCESS_WRITE);
  for_rows(ma.rows, [&](int y) {
    const float* pa = ma.ptr<float>(y);
    const float* pb = mb.ptr<float>(y);
    const float* paa = aa.ptr<float>(y);
    const float* pbb = bb.ptr<float>(y);
    const float* pab = ab.ptr<float>(y);
    float* o = om.ptr<float>(y);
    for(int x = 0; x < ma.cols; ++x)
    {
      const float va = paa[x] - pa[x] * pa[x];
      const float vb = pbb[x] - pb[x] * pb[x];
      const float cov = pab[x] - pa[x] * pb[x];
      const float ssim = ((2.0f * pa[x] * pb[x] + c1) * (2.0f * cov + c2)) / ((pa[x] * pa[x] + pb[x] * pb[x] + c1) * (va + vb + c2));
      o[x] = std::clamp(1.0f - ssim, 0.0f, 1.0f);
    }
  });
}

void to_linear_premul(const cv::UMat& src, cv::UMat& dst)
{
  CC_REQUIRE(src.type() == CV_8UC4, return);
  dst.create(src.size(), CV_32FC4);
  if(want_gpu())
  {
    cv::ocl::Kernel k = make_kernel("srgb8_to_linear_premul");
    const cv::UMat lut = device_lut();
    if(!k.empty())
    {
      k.args(cv::ocl::KernelArg::ReadOnly(src), cv::ocl::KernelArg::WriteOnlyNoSize(dst), cv::ocl::KernelArg::PtrReadOnly(lut));
      if(run2d(k, src.cols, src.rows))
      {
        return;
      }
    }
  }
  const float* lut = srgb_to_linear_table();
  const cv::Mat sm = src.getMat(cv::ACCESS_READ);
  cv::Mat dm = dst.getMat(cv::ACCESS_WRITE);
  for_rows(sm.rows, [&](int y) {
    const uint8_t* p = sm.ptr<uint8_t>(y);
    float* o = dm.ptr<float>(y);
    for(int x = 0; x < sm.cols; ++x)
    {
      const uint8_t* pp = p + px_off(x);
      float* oo = o + px_off(x);
      const float al = static_cast<float>(pp[3]) / kMaxLevel;
      oo[0] = lut[pp[0]] * al;
      oo[1] = lut[pp[1]] * al;
      oo[2] = lut[pp[2]] * al;
      oo[3] = al;
    }
  });
}

void from_linear_premul(const cv::UMat& src, cv::UMat& dst)
{
  CC_REQUIRE(src.type() == CV_32FC4, return);
  dst.create(src.size(), CV_8UC4);
  if(want_gpu())
  {
    cv::ocl::Kernel k = make_kernel("linear_premul_to_srgb8");
    if(!k.empty())
    {
      k.args(cv::ocl::KernelArg::ReadOnly(src), cv::ocl::KernelArg::WriteOnlyNoSize(dst));
      if(run2d(k, src.cols, src.rows))
      {
        return;
      }
    }
  }
  const cv::Mat sm = src.getMat(cv::ACCESS_READ);
  cv::Mat dm = dst.getMat(cv::ACCESS_WRITE);
  for_rows(sm.rows, [&](int y) {
    const float* p = sm.ptr<float>(y);
    uint8_t* o = dm.ptr<uint8_t>(y);
    for(int x = 0; x < sm.cols; ++x)
    {
      const float* pp = p + px_off(x);
      uint8_t* oo = o + px_off(x);
      const float al = std::clamp(pp[3], 0.0f, 1.0f);
      const float inv = al > 0.0f ? 1.0f / al : 0.0f;
      oo[0] = to_level(linear_to_srgb(pp[0] * inv));
      oo[1] = to_level(linear_to_srgb(pp[1] * inv));
      oo[2] = to_level(linear_to_srgb(pp[2] * inv));
      oo[3] = to_level(al);
    }
  });
}

/* ---- build and self-test ------------------------------------------------ */

bool build_kernels(std::string& err)
{
  g_kernels_ready = false;
  if(!cv::ocl::haveOpenCL())
  {
    err = "no OpenCL runtime";
    return false;
  }
  const char* const names[] = {"pixel_metric", "pair_maps", "luma_pair", "ssim_combine", "srgb8_to_linear_premul",
                               "linear_premul_to_srgb8"};
  for(const char* name : names)
  {
    cv::String log;
    cv::ocl::Kernel k(name, program(), cv::String(), &log);
    if(k.empty())
    {
      err = std::string("OpenCL kernel '") + name + "' did not build";
      if(!log.empty())
      {
        err += ": " + std::string(log.c_str());
      }
      return false;
    }
  }
  g_kernels_ready = true;
  return true;
}

bool kernels_ready() { return g_kernels_ready.load(); }

unsigned long gpu_launches() { return g_gpu_launches.load(); }

namespace {

/** @brief The self-test pair: saturated colours, greys, gradients and partial alpha, and a perturbed copy. */
void self_test_pattern(Image& pa, Image& pb)
{
  pa = Image::blank(kSelfTestW, kSelfTestH);
  pb = Image::blank(kSelfTestW, kSelfTestH);
  /* NOLINTBEGIN(readability-magic-numbers): arbitrary coefficients of a synthetic test pattern */
  for(int y = 0; y < kSelfTestH; ++y)
  {
    for(int x = 0; x < kSelfTestW; ++x)
    {
      uint8_t* p = pa.px(x, y);
      uint8_t* q = pb.px(x, y);
      p[0] = static_cast<uint8_t>((x * 37 + y * 11) & 0xFF);
      p[1] = static_cast<uint8_t>((x * 5 + y * 53) & 0xFF);
      p[2] = static_cast<uint8_t>((x * y * 7) & 0xFF);
      p[3] = static_cast<uint8_t>(255 - ((x + y) & 0x3F));
      q[0] = static_cast<uint8_t>(std::clamp(p[0] + ((x % 7) - 3) * 4, 0, 255));
      q[1] = static_cast<uint8_t>(std::clamp(p[1] + ((y % 5) - 2) * 6, 0, 255));
      q[2] = static_cast<uint8_t>((p[2] + 9) & 0xFF);
      q[3] = p[3];
    }
  }
  /* NOLINTEND(readability-magic-numbers) */
}

/** @brief Runs one operation with OpenCL on and off and counts the differing samples. */
struct SelfTestRun
{
  const Image& pa;
  const Image& pb;
  float tolerance;
  long bad = 0;
  long total = 0;

  template <typename Op>
  void compare(const Op& op)
  {
    cv::ocl::setUseOpenCL(true);
    const cv::UMat ga = upload(pa);
    const cv::UMat gb = upload(pb);
    cv::UMat g_out;
    op(ga, gb, g_out);
    cv::ocl::setUseOpenCL(false);
    const cv::UMat ca = upload(pa);
    const cv::UMat cb = upload(pb);
    cv::UMat c_out;
    op(ca, cb, c_out);
    const long n = count_mismatches(g_out, c_out, tolerance);
    const long samples = static_cast<long>(c_out.total()) * c_out.channels();
    total += samples;
    bad += n < 0 ? samples : n;
  }
};

/** @brief SSIM combination fed with luma moments (no blur: the kernel is what is tested). */
void ssim_of_luma(const cv::UMat& a, const cv::UMat& b, cv::UMat& o)
{
  cv::UMat ya;
  cv::UMat yb;
  luma_pair(a, b, ya, yb);
  cv::UMat e_aa;
  cv::UMat e_bb;
  cv::UMat e_ab;
  cv::multiply(ya, ya, e_aa);
  cv::multiply(yb, yb, e_bb);
  cv::multiply(ya, yb, e_ab);
  ssim_combine(ya, yb, e_aa, e_bb, e_ab, kSelfTestC1, kSelfTestC2, o);
}

} // namespace

float self_test(float tolerance)
{
  if(!kernels_ready())
  {
    return 1.0f;
  }
  Image pa;
  Image pb;
  self_test_pattern(pa, pb);
  SelfTestRun run{pa, pb, tolerance};
  const bool was = cv::ocl::useOpenCL();
  for(int m = 0; m < kMetricCount; ++m)
  {
    if(static_cast<Metric>(m) != Metric::SSIM)
    {
      run.compare([m](const cv::UMat& a, const cv::UMat& b, cv::UMat& o) { metric_map(a, b, static_cast<Metric>(m), o); });
    }
  }
  cv::UMat s;
  cv::UMat d;
  run.compare([&](const cv::UMat& a, const cv::UMat& b, cv::UMat& o) { pair_maps(a, b, o, s, d); });
  run.compare([&](const cv::UMat& a, const cv::UMat& b, cv::UMat& o) { pair_maps(a, b, s, d, o); });
  run.compare([&](const cv::UMat& a, const cv::UMat& b, cv::UMat& o) { luma_pair(a, b, o, s); });
  run.compare(ssim_of_luma);
  run.compare([&](const cv::UMat& a, const cv::UMat&, cv::UMat& o) {
    to_linear_premul(a, s);
    from_linear_premul(s, o);
  });
  cv::ocl::setUseOpenCL(was);
  return run.total > 0 ? static_cast<float>(run.bad) / static_cast<float>(run.total) : 1.0f;
}

} // namespace cc::cvops
