/**
 * @file metric.cpp
 * @brief Names, units, help texts and configuration keys of the metrics.
 */
#include "core/metric.h"

namespace cc {

const char* metric_name(Metric m)
{
  switch(m)
  {
    case Metric::AbsDiff: return "Absolute difference (max RGB)";
    case Metric::LumaDiff: return "Luma difference";
    case Metric::DeltaE76: return "Colour difference dE76";
    case Metric::DeltaE2000: return "Colour difference dE2000";
    case Metric::SSIM: return "Structural loss (1 - SSIM)";
    case Metric::SquaredError: return "Squared error";
    case Metric::COUNT: break;
  }
  return "?";
}

const char* metric_unit(Metric m)
{
  switch(m)
  {
    case Metric::AbsDiff:
    case Metric::LumaDiff: return "levels (0-255)";
    case Metric::DeltaE76:
    case Metric::DeltaE2000: return "dE";
    case Metric::SSIM: return "1-SSIM";
    case Metric::SquaredError: return "levels^2";
    case Metric::COUNT: break;
  }
  return "";
}

const char* metric_help(Metric m)
{
  switch(m)
  {
    case Metric::AbsDiff:
      return "Largest change among the R, G and B channels of each pixel, in 8-bit levels.\n"
             "Cheap and literal: shows every altered pixel, including invisible ones.\n"
             "CLIJ2: absoluteDifference (subtractImages + absolute).";
    case Metric::LumaDiff:
      return "Change in Rec.601 luma (0.299R + 0.587G + 0.114B).\n"
             "Ignores chroma, so 4:2:0 chroma subsampling barely shows here.";
    case Metric::DeltaE76:
      return "Euclidean distance in CIELAB (D65). Roughly: 1 = just noticeable, 2-3 = visible side by side,\n"
             "> 5 = obvious. Cheap but over-weights saturated colours.";
    case Metric::DeltaE2000:
      return "CIEDE2000, the perceptual colour-difference standard (Sharma 2005 formulation).\n"
             "~1 = just noticeable, 2.3 = JND used in the stats, 5+ = clearly visible.\n"
             "Best single metric for 'will anyone see this?'.";
    case Metric::SSIM:
      return "1 minus the Structural Similarity index (Wang et al. 2004) on luma over a Gaussian window\n"
             "(size and sigma from config.yaml, metrics.ssim). Highlights blurred texture, ringing and\n"
             "blocking rather than uniform shifts. 0 = identical structure, 1 = unrelated.\n"
             "Built from CLIJ2-style mean/variance statistics (gaussian_blur_separable).";
    case Metric::SquaredError:
      return "Mean over R, G, B of the squared difference. Its image-wide mean is the MSE behind PSNR.\n"
             "CLIJ2: squaredDifference / meanSquaredError.";
    case Metric::COUNT: break;
  }
  return "";
}

const char* metric_key(Metric m)
{
  switch(m)
  {
    case Metric::AbsDiff: return "abs_diff";
    case Metric::LumaDiff: return "luma_diff";
    case Metric::DeltaE76: return "delta_e76";
    case Metric::DeltaE2000: return "delta_e2000";
    case Metric::SSIM: return "ssim";
    case Metric::SquaredError: return "squared_error";
    case Metric::COUNT: break;
  }
  return "";
}

bool metric_from_key(const std::string& key, Metric& out)
{
  for(int i = 0; i < kMetricCount; ++i)
  {
    const Metric m = static_cast<Metric>(i);
    if(key == metric_key(m))
    {
      out = m;
      return true;
    }
  }
  return false;
}

} // namespace cc
