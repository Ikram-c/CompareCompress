/**
 * @file cv_ops.h
 * @brief The custom image operations of the pipeline, each with an OpenCL
 * kernel (core/cl_kernels.h) and an identical CPU implementation.
 *
 * All functions take and return `cv::UMat`.  Whether a call runs on the GPU
 * is decided by OpenCV's per-thread switch (`cv::ocl::useOpenCL()`, set by
 * use_gpu_for()); when a kernel cannot run the CPU twin is used instead, so a
 * caller never has to handle an OpenCL failure.  Internal to the core
 * library: the app and the tests use image.h and metrics.h.
 */
#pragma once

#include "core/image.h"
#include "core/metric.h"

#include <opencv2/core.hpp>

#include <string>

namespace cc::cvops {

/** @brief Copies an Image into a CV_8UC4 UMat (R, G, B, A order kept). */
[[nodiscard]] cv::UMat upload(const Image& img);

/** @brief Copies a CV_8UC4 UMat back into an Image. */
[[nodiscard]] Image download(const cv::UMat& m);

/** @brief Copies a CV_32FC1 UMat into a row-major float vector. */
void download(const cv::UMat& m, std::vector<float>& out);

/**
 * @brief One per-pixel metric map (CV_32FC1).
 * @param a Original, CV_8UC4.
 * @param b Site copy, CV_8UC4, same size.
 * @param m Any metric except Metric::SSIM.
 * @param out Receives the map.
 */
void metric_map(const cv::UMat& a, const cv::UMat& b, Metric m, cv::UMat& out);

/** @brief The three maps compute_stats() reduces (max-channel |diff|, squared error, CIEDE2000). */
void pair_maps(const cv::UMat& a, const cv::UMat& b, cv::UMat& abs_map, cv::UMat& sq_map, cv::UMat& de_map);

/** @brief Rec.601 luma of both images as CV_32FC1 (levels 0..255). */
void luma_pair(const cv::UMat& a, const cv::UMat& b, cv::UMat& ya, cv::UMat& yb);

/** @brief 1 - SSIM per pixel from the filtered moments (all CV_32FC1). */
void ssim_combine(const cv::UMat& mu_a, const cv::UMat& mu_b, const cv::UMat& e_aa, const cv::UMat& e_bb, const cv::UMat& e_ab,
                  float c1, float c2, cv::UMat& out);

/** @brief CV_8UC4 sRGB to CV_32FC4 linear light, colour premultiplied by alpha. */
void to_linear_premul(const cv::UMat& src, cv::UMat& dst);

/** @brief Inverse of to_linear_premul(): CV_32FC4 to CV_8UC4 (clamped, rounded). */
void from_linear_premul(const cv::UMat& src, cv::UMat& dst);

/**
 * @brief Builds the OpenCL program on the current default device.
 * @param err Receives the build log on failure.
 * @return true when every kernel is available.
 */
[[nodiscard]] bool build_kernels(std::string& err);

/** @brief True after a successful build_kernels(). */
[[nodiscard]] bool kernels_ready();

/** @brief Number of custom kernels launched on the OpenCL device so far (diagnostics and tests). */
[[nodiscard]] unsigned long gpu_launches();

/**
 * @brief Runs every kernel and its CPU twin on a synthetic pattern.
 * @param tolerance Largest accepted |GPU - CPU| per sample (8-bit results in units of 1/255).
 * @return Fraction of samples (0..1) outside the tolerance; 1 when the kernels are not built.
 */
[[nodiscard]] float self_test(float tolerance);

} // namespace cc::cvops
