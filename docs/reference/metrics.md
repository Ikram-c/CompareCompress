# Metrics and statistics

Everything is computed on 8-bit sRGB pixels of the two images **at the comparison size** (see
[how a comparison works](../explanation/how-it-works.md)). Per-pixel metrics feed the heat map; image-wide
statistics fill the *Statistics* panel. The implementations are in `src/core/metrics.cpp`, with the
per-pixel formulas as OpenCL kernels in `src/core/cl_kernels.h` and identical CPU versions in
`src/core/cv_ops.cpp` (which one runs is described in [how to use the GPU](../how-to/use-the-gpu.md)); the
tunable constants are in the `metrics` section of [config.yaml](config.md).

## Per-pixel metrics (heat map)

| Metric (`heatmap.metric` key) | Value at a pixel | Unit | Reading |
|---|---|---|---|
| Absolute difference (`abs_diff`) | max over R, G, B of \|after − before\| | levels, 0–255 | literal and cheap; shows every altered pixel, invisible ones included |
| Luma difference (`luma_diff`) | \|Y′(after) − Y′(before)\| with Rec. 601 luma Y′ = 0.299 R + 0.587 G + 0.114 B | levels, 0–255 | ignores chroma, so 4:2:0 subsampling barely registers |
| Colour difference ΔE76 (`delta_e76`) | Euclidean distance in CIELAB | ΔE | ≈1 just noticeable, 2–3 visible side by side, >5 obvious; over-weights saturated colours |
| Colour difference ΔE2000 (`delta_e2000`) | CIEDE2000 (Sharma, Wu & Dalal 2005 formulation, all branches incl. h′ sum ≥ 360°) | ΔE | the perceptual standard; ≈1 just noticeable, 2.3 = the JND used in the statistics, 5+ clearly visible |
| Structural loss (`ssim`) | 1 − SSIM on luma over a Gaussian window (`metrics.ssim.window` = 11 px, `sigma` = 1.5, k1 = 0.01, k2 = 0.03, L = 255; Wang et al. 2004) | 1 − SSIM, 0–1 | highlights blur, ringing and blocking rather than uniform shifts; 0 = identical structure |
| Squared error (`squared_error`) | mean over R, G, B of (after − before)² | levels² | its image-wide mean is the MSE behind PSNR |

CIELAB values come from sRGB with the IEC 61966-2-1 transfer curve, the D65 sRGB→XYZ matrix and the CIE
1976 L\*a\*b\* equations with ε = 216/24389 and κ = 24389/27. Both images are sRGB, so no chromatic
adaptation is applied. The ΔE2000 implementation is checked against the published Sharma–Wu–Dalal test
pairs in `tests/test_metrics.cpp`.

### Derived quantities shown with the map

| Quantity | Definition |
|---|---|
| min, max, mean | over all pixels of the map |
| median, p95, p99 | percentiles, estimated from a `metrics.histogram_fine_bins` (1024) bin histogram over [0, max] |
| auto scale maximum | the `metrics.auto_scale_percentile` (99) percentile; the colour ramp maps 0 … this value to the colour map, values above saturate |
| histogram | `metrics.histogram_bins` (64) bins over [0, max], linear or log count (*Statistics* panel) |
| block view | every aligned `metrics.block_size_px` × `metrics.block_size_px` (8×8) cell replaced by its mean, then the statistics above recomputed |

Colour mapping of a value *v*: `t = clamp(v / max, 0, 1) ^ gamma`, then the selected colour map at *t*.
The overlay blends that colour over the *After* image with the intensity slider; in *proportional* mode the
opacity is additionally multiplied by *t* so unchanged areas stay clear; the cut-off hides pixels below a
threshold.

## Image-wide statistics (*Statistics* panel)

| Row | Definition | Reading |
|---|---|---|
| PSNR | 10 · log10(255² / MSE) with MSE the mean squared error over all R, G, B samples; identical images report `metrics.psnr_identical_db` (99 dB) instead of infinity | > 45 dB visually lossless; 35–45 good; 30–35 visible on close inspection; < 30 obvious artefacts |
| SSIM | mean of the per-pixel SSIM map (luma, Gaussian window as above) | 1 identical structure; > 0.98 excellent; ≈0.95 good; < 0.90 clearly degraded textures and edges |
| ΔE2000 mean / max | mean and maximum of the per-pixel CIEDE2000 | mean < 1 is essentially invisible |
| visible changes | share of pixels with ΔE2000 > `metrics.jnd_delta_e2000` (2.3) | how much of the picture a viewer could notice |
| untouched pixels | share of pixels that came back bit-identical | high for lossless paths, ≈0 after any JPEG re-encode or resize |
| mean abs diff | mean of the absolute-difference metric | in 0–255 levels |
| size, file size, format | dimensions and bytes original → site, container/codec sniffed from the bytes | most sites cap the long edge (1080, 2048, 4096) |
| site JPEG / original JPEG | estimated IJG quality, chroma subsampling, baseline/progressive, custom tables | see below |
| metadata kept | whether EXIF and an ICC profile survived | *none (stripped)* is the norm for social media |
| video, frame rate, frames compared | codec and bit rate, fps, and the timestamps of the two compared frames (videos only) | from the video back-end; with OpenCV the bit rate is the file's average (size ÷ duration) |

## JPEG quality estimate

The site copy's DQT (quantisation) tables are read from the file. libjpeg-style encoders scale the IJG
Annex K example tables by a single factor derived from the quality setting (`jpeg_quality_scaling()`):
scale = 5000/q for q < 50, otherwise 200 − 2q, and each table entry is ⌊(base × scale + 50) / 100⌋
clamped to 1…255. The app tries every quality from 1 to 100 per table, keeps the one whose predicted
table is closest to the observed one (for luma and for chroma separately), and reports the fit error – the
mean absolute deviation of the 64 entries as a percentage of the table's mean. When the fit error exceeds
`metrics.custom_table_fit_error_pct` (8 %), the tables did not come from that formula (mozjpeg, Photoshop
*Save for Web*, camera firmware, Guetzli) and the row says *custom tables*; the number is then only
indicative. Subsampling (4:4:4, 4:2:2, 4:2:0, 4:4:0, 4:1:1, gray) is read from the SOF sampling factors;
*progressive* from the SOF2 marker; metadata from the APP1 (EXIF) and APP2 (ICC) segments.

## Video sampling

*Sample quality over time* pulls `ffmpeg.sample_points` (8 by default, adjustable between
`sample_points_min` = 3 and `sample_points_max` = 24) frames from each video at the same *relative*
positions – sample *i* of *n* sits at (i + ½)/n of each clip's own duration, so a site that trims a clip
still lines up approximately – resamples each pair like stills and plots PSNR and SSIM per sample. Frames
are decoded by the video back-end (OpenCV's AVFoundation or FFmpeg reader, or the `ffmpeg` executable) at
those timestamps; a changed frame rate makes the nearest frames compare.

## References

- Wang, Bovik, Sheikh & Simoncelli, *Image quality assessment: from error visibility to structural
  similarity*, IEEE TIP 13(4), 2004 – SSIM, the 11×11 σ = 1.5 window and k1 = 0.01, k2 = 0.03.
- Sharma, Wu & Dalal, *The CIEDE2000 color-difference formula: implementation notes, supplementary test
  data, and mathematical observations*, Color Res. Appl. 30(1), 2005 – ΔE2000 and its test pairs.
- IEC 61966-2-1:1999 – sRGB.
- ITU-R BT.601 – luma coefficients.
- Independent JPEG Group, `jcparam.c` – `jpeg_quality_scaling()` and the Annex K tables.
- Haase et al., *CLIJ: GPU-accelerated image processing for everyone*, Nature Methods 17, 2020 – the
  CLIJ2 kernels (absoluteDifference, squaredDifference, meanSquaredError, gaussian_blur_separable,
  variance/mean box statistics) that the per-pixel code was modelled on.
