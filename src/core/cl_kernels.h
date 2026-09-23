/**
 * @file cl_kernels.h
 * @brief OpenCL C source of the kernels OpenCV has no built-in for.
 *
 * Written for OpenCL C 1.2 (what macOS 12 provides on Intel HD / Iris
 * graphics): no double precision, no images, no extensions.  Every kernel
 * takes OpenCV `KernelArg` triples (pointer, step in bytes, offset in bytes),
 * the first one followed by rows and cols, and has a CPU twin in cv_ops.cpp
 * that the self-test compares it with.  The colour constants are the same
 * standards constants as in metrics.cpp.
 */
#pragma once

namespace cc {

/** @brief Program source handed to cv::ocl::ProgramSource. */
constexpr const char* kClKernelSource = R"CLC(
/* ---- constants (IEC 61966-2-1, CIE Lab, CIEDE2000, Rec.601) ------------- */
#define CC_PI          3.14159265358979323846f
#define CC_MAX_LEVEL   255.0f
#define CC_XR 0.4124564f
#define CC_XG 0.3575761f
#define CC_XB 0.1804375f
#define CC_YR 0.2126729f
#define CC_YG 0.7151522f
#define CC_YB 0.0721750f
#define CC_ZR 0.0193339f
#define CC_ZG 0.1191920f
#define CC_ZB 0.9503041f
#define CC_XN 0.95047f
#define CC_YN 1.0f
#define CC_ZN 1.08883f
#define CC_LAB_EPS   (216.0f / 24389.0f)
#define CC_LAB_KAPPA (24389.0f / 27.0f)
#define CC_POW25_7   6103515625.0f
#define CC_LUMA_R 0.299f
#define CC_LUMA_G 0.587f
#define CC_LUMA_B 0.114f
#define CC_SRGB_LIN_CUT 0.0031308f

enum { M_ABS = 0, M_LUMA = 1, M_DE76 = 2, M_DE2000 = 3, M_SSIM = 4, M_SQ = 5 };

float lab_f(float t)
{
  return t > CC_LAB_EPS ? cbrt(t) : (CC_LAB_KAPPA * t + 16.0f) / 116.0f;
}

float3 to_lab(__global const float* lut, uchar4 p)
{
  const float r = lut[p.x];
  const float g = lut[p.y];
  const float b = lut[p.z];
  const float X = CC_XR * r + CC_XG * g + CC_XB * b;
  const float Y = CC_YR * r + CC_YG * g + CC_YB * b;
  const float Z = CC_ZR * r + CC_ZG * g + CC_ZB * b;
  const float fx = lab_f(X / CC_XN);
  const float fy = lab_f(Y / CC_YN);
  const float fz = lab_f(Z / CC_ZN);
  return (float3)(116.0f * fy - 16.0f, 500.0f * (fx - fy), 200.0f * (fy - fz));
}

float deg2rad(float d) { return d * (CC_PI / 180.0f); }
float rad2deg(float r) { return r * (180.0f / CC_PI); }

float hue_degrees(float a, float b)
{
  if(a == 0.0f && b == 0.0f) return 0.0f;
  float h = rad2deg(atan2(b, a));
  return h < 0.0f ? h + 360.0f : h;
}

float hue_difference(float h1, float h2, float cp)
{
  if(cp == 0.0f) return 0.0f;
  const float d = h2 - h1;
  if(fabs(d) <= 180.0f) return d;
  return d > 180.0f ? d - 360.0f : d + 360.0f;
}

float hue_mean(float h1, float h2, float cp)
{
  if(cp == 0.0f) return h1 + h2;
  if(fabs(h1 - h2) <= 180.0f) return 0.5f * (h1 + h2);
  return h1 + h2 < 360.0f ? 0.5f * (h1 + h2 + 360.0f) : 0.5f * (h1 + h2 - 360.0f);
}

float pow7(float x) { const float x2 = x * x; const float x3 = x2 * x; return x3 * x3 * x; }

float delta_e76(float3 x, float3 y)
{
  const float3 d = x - y;
  return sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
}

float delta_e2000(float3 x, float3 y)
{
  const float C1 = sqrt(x.y * x.y + x.z * x.z);
  const float C2 = sqrt(y.y * y.y + y.z * y.z);
  const float Cbar7 = pow7(0.5f * (C1 + C2));
  const float G = 0.5f * (1.0f - sqrt(Cbar7 / (Cbar7 + CC_POW25_7)));
  const float a1p = (1.0f + G) * x.y;
  const float a2p = (1.0f + G) * y.y;
  const float C1p = sqrt(a1p * a1p + x.z * x.z);
  const float C2p = sqrt(a2p * a2p + y.z * y.z);
  const float cp = C1p * C2p;
  const float h1p = hue_degrees(a1p, x.z);
  const float h2p = hue_degrees(a2p, y.z);
  const float dLp = y.x - x.x;
  const float dCp = C2p - C1p;
  const float dhp = hue_difference(h1p, h2p, cp);
  const float dHp = 2.0f * sqrt(cp) * sin(deg2rad(dhp * 0.5f));
  const float Lbp = 0.5f * (x.x + y.x);
  const float Cbp = 0.5f * (C1p + C2p);
  const float hbp = hue_mean(h1p, h2p, cp);
  const float T = 1.0f - 0.17f * cos(deg2rad(hbp - 30.0f)) + 0.24f * cos(deg2rad(2.0f * hbp))
                + 0.32f * cos(deg2rad(3.0f * hbp + 6.0f)) - 0.20f * cos(deg2rad(4.0f * hbp - 63.0f));
  const float ta = (hbp - 275.0f) / 25.0f;
  const float dtheta = 30.0f * exp(-ta * ta);
  const float Cbp7 = pow7(Cbp);
  const float RC = 2.0f * sqrt(Cbp7 / (Cbp7 + CC_POW25_7));
  const float Ldev = (Lbp - 50.0f) * (Lbp - 50.0f);
  const float SL = 1.0f + (0.015f * Ldev) / sqrt(20.0f + Ldev);
  const float SC = 1.0f + 0.045f * Cbp;
  const float SH = 1.0f + 0.015f * Cbp * T;
  const float RT = -sin(deg2rad(2.0f * dtheta)) * RC;
  const float tL = dLp / SL;
  const float tC = dCp / SC;
  const float tH = dHp / SH;
  return sqrt(fmax(0.0f, tL * tL + tC * tC + tH * tH + RT * tC * tH));
}

float luma601(uchar4 p) { return CC_LUMA_R * (float)p.x + CC_LUMA_G * (float)p.y + CC_LUMA_B * (float)p.z; }

int max_channel_difference(uchar4 p, uchar4 q)
{
  const int dr = abs((int)p.x - (int)q.x);
  const int dg = abs((int)p.y - (int)q.y);
  const int db = abs((int)p.z - (int)q.z);
  return max(dr, max(dg, db));
}

float squared_error(uchar4 p, uchar4 q)
{
  const float dr = (float)((int)p.x - (int)q.x);
  const float dg = (float)((int)p.y - (int)q.y);
  const float db = (float)((int)p.z - (int)q.z);
  return (dr * dr + dg * dg + db * db) / 3.0f;
}

#define PIX4(ptr, step, off, x, y) vload4(0, (ptr) + mad24((y), (step), (off) + (x) * 4))
#define FPTR(ptr, step, off, x, y) ((__global float*)((ptr) + mad24((y), (step), (off) + (x) * 4)))
#define CFPTR(ptr, step, off, x, y) ((__global const float*)((ptr) + mad24((y), (step), (off) + (x) * 4)))

/* One per-pixel metric (everything except SSIM) into a float map. */
__kernel void pixel_metric(__global const uchar* a, int a_step, int a_off, int rows, int cols,
                           __global const uchar* b, int b_step, int b_off,
                           __global uchar* d, int d_step, int d_off,
                           __global const float* lut, int metric)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);
  if(x >= cols || y >= rows) return;
  const uchar4 p = PIX4(a, a_step, a_off, x, y);
  const uchar4 q = PIX4(b, b_step, b_off, x, y);
  float v = 0.0f;
  if(metric == M_ABS)        v = (float)max_channel_difference(p, q);
  else if(metric == M_LUMA)  v = fabs(luma601(p) - luma601(q));
  else if(metric == M_DE76)  v = delta_e76(to_lab(lut, p), to_lab(lut, q));
  else if(metric == M_DE2000) v = delta_e2000(to_lab(lut, p), to_lab(lut, q));
  else if(metric == M_SQ)    v = squared_error(p, q);
  *FPTR(d, d_step, d_off, x, y) = v;
}

/* The three maps compute_stats() reduces: max-channel |diff|, squared error, CIEDE2000. */
__kernel void pair_maps(__global const uchar* a, int a_step, int a_off, int rows, int cols,
                        __global const uchar* b, int b_step, int b_off,
                        __global uchar* abs_d, int abs_step, int abs_off,
                        __global uchar* sq_d, int sq_step, int sq_off,
                        __global uchar* de_d, int de_step, int de_off,
                        __global const float* lut)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);
  if(x >= cols || y >= rows) return;
  const uchar4 p = PIX4(a, a_step, a_off, x, y);
  const uchar4 q = PIX4(b, b_step, b_off, x, y);
  *FPTR(abs_d, abs_step, abs_off, x, y) = (float)max_channel_difference(p, q);
  *FPTR(sq_d, sq_step, sq_off, x, y) = squared_error(p, q);
  *FPTR(de_d, de_step, de_off, x, y) = delta_e2000(to_lab(lut, p), to_lab(lut, q));
}

/* Rec.601 luma of both images. */
__kernel void luma_pair(__global const uchar* a, int a_step, int a_off, int rows, int cols,
                        __global const uchar* b, int b_step, int b_off,
                        __global uchar* ya, int ya_step, int ya_off,
                        __global uchar* yb, int yb_step, int yb_off)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);
  if(x >= cols || y >= rows) return;
  *FPTR(ya, ya_step, ya_off, x, y) = luma601(PIX4(a, a_step, a_off, x, y));
  *FPTR(yb, yb_step, yb_off, x, y) = luma601(PIX4(b, b_step, b_off, x, y));
}

/* 1 - SSIM from the five Gaussian-filtered moments. */
__kernel void ssim_combine(__global const uchar* mu_a, int ma_step, int ma_off, int rows, int cols,
                           __global const uchar* mu_b, int mb_step, int mb_off,
                           __global const uchar* e_aa, int aa_step, int aa_off,
                           __global const uchar* e_bb, int bb_step, int bb_off,
                           __global const uchar* e_ab, int ab_step, int ab_off,
                           __global uchar* d, int d_step, int d_off,
                           float C1, float C2)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);
  if(x >= cols || y >= rows) return;
  const float ma = *CFPTR(mu_a, ma_step, ma_off, x, y);
  const float mb = *CFPTR(mu_b, mb_step, mb_off, x, y);
  const float va = *CFPTR(e_aa, aa_step, aa_off, x, y) - ma * ma;
  const float vb = *CFPTR(e_bb, bb_step, bb_off, x, y) - mb * mb;
  const float cov = *CFPTR(e_ab, ab_step, ab_off, x, y) - ma * mb;
  const float ssim = ((2.0f * ma * mb + C1) * (2.0f * cov + C2)) / ((ma * ma + mb * mb + C1) * (va + vb + C2));
  *FPTR(d, d_step, d_off, x, y) = clamp(1.0f - ssim, 0.0f, 1.0f);
}

/* 8-bit sRGB RGBA -> linear-light RGBA float, colour premultiplied by alpha. */
__kernel void srgb8_to_linear_premul(__global const uchar* s, int s_step, int s_off, int rows, int cols,
                                     __global uchar* d, int d_step, int d_off,
                                     __global const float* lut)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);
  if(x >= cols || y >= rows) return;
  const uchar4 p = PIX4(s, s_step, s_off, x, y);
  const float al = (float)p.w / CC_MAX_LEVEL;
  const float4 v = (float4)(lut[p.x] * al, lut[p.y] * al, lut[p.z] * al, al);
  vstore4(v, 0, (__global float*)(d + mad24(y, d_step, d_off + x * 16)));
}

float linear_to_srgb(float c)
{
  c = clamp(c, 0.0f, 1.0f);
  return c <= CC_SRGB_LIN_CUT ? 12.92f * c : 1.055f * pow(c, 1.0f / 2.4f) - 0.055f;
}

uchar to_level(float c) { return (uchar)clamp(floor(c * CC_MAX_LEVEL + 0.5f), 0.0f, CC_MAX_LEVEL); }

/* Inverse of srgb8_to_linear_premul (un-premultiply, encode, round). */
__kernel void linear_premul_to_srgb8(__global const uchar* s, int s_step, int s_off, int rows, int cols,
                                     __global uchar* d, int d_step, int d_off)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);
  if(x >= cols || y >= rows) return;
  const float4 v = vload4(0, (__global const float*)(s + mad24(y, s_step, s_off + x * 16)));
  const float al = clamp(v.w, 0.0f, 1.0f);
  const float inv = al > 0.0f ? 1.0f / al : 0.0f;
  const uchar4 o = (uchar4)(to_level(linear_to_srgb(v.x * inv)), to_level(linear_to_srgb(v.y * inv)),
                            to_level(linear_to_srgb(v.z * inv)), to_level(al));
  vstore4(o, 0, d + mad24(y, d_step, d_off + x * 4));
}
)CLC";

} // namespace cc
