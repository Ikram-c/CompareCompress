# How a comparison works

This page explains what happens between *Visualize compression* and the heat map, and why it is done that
way. For the exact formulas see [Metrics](../reference/metrics.md); for the code map see
[Architecture](../reference/architecture.md).

## The problem: two files that are not the same size

A site rarely returns the pixels you sent. It resizes to a cap (1080, 2048, 4096 px), sometimes crops to a
forced aspect ratio, converts the format, re-encodes with its own quality and chroma subsampling, and
strips metadata. To draw a per-pixel difference the two images must have the same dimensions, so one of
them has to be resampled – and which one you resample changes what the heat map shows.

### Compare at the site's resolution (default)

The original is scaled **down** to the site copy's size. The comparison then answers *what do viewers
receive compared with the best possible rendition at that size?* The resize itself is not counted as
damage; only what the encoder did on top of it. This is what you usually want: nobody sees the 4000-pixel
original on Instagram, and a downscale done well is not the site's fault.

### Compare at the original's resolution

Untick *compare at site size* (Settings, or `app.compare_at_site_resolution`) and the site copy is scaled
**up** to the original. Now the loss of resolution shows as blur over the whole picture, on top of the
encoder's artefacts, and PSNR/SSIM drop accordingly. Use this to see the *total* damage.

### Resampling in linear light

Both directions are sRGB-aware: the 8-bit sRGB values are converted to linear light with the colour
premultiplied by alpha, resized with OpenCV's `cv::resize`, and converted back. When the image shrinks in
both directions the filter is `INTER_AREA`, which averages every source pixel that falls into a target pixel
(a proper box pre-filter, so fine detail does not alias); otherwise it is bicubic (`INTER_CUBIC`). Filtering
in gamma space darkens edges and would show up as a false error signal along every contour: a black-and-white
checkerboard shrunk in linear light averages to sRGB 188, not 128, and `test_opencl` checks exactly that.

### When the aspect ratio changed

If the two aspect ratios differ by more than `metrics.aspect_mismatch_tolerance` (0.5 %), the site cropped.
The image being resampled is then scaled to *cover* the target and centre-cropped, which reproduces what
most sites do. Sites do not always crop dead-centre; the *Statistics* panel shows an alignment nudge (an x/y
offset in pixels) when this happens, and the heat map tells you immediately whether the alignment is right
– a misaligned pair lights up along every edge.

### The texture ceiling

The comparison size is finally capped at the GPU's maximum texture size (`GL_MAX_TEXTURE_SIZE`, 16 384 on
most hardware), keeping the aspect ratio, so that a huge original does not fail at upload time.

## What is computed

1. **Statistics** on the pair: MSE → PSNR, mean SSIM, mean and maximum ΔE2000, share of pixels above the
   just-noticeable difference, share of untouched pixels, mean absolute difference.
2. **The heat map** for the selected metric: one `float` per pixel, then min / max / mean, a fine histogram
   for the percentiles (median, p95, p99, and the auto-scale percentile), and a coarse histogram for the
   panel. Block view replaces each 8×8 cell by its mean before these statistics are taken.
3. Only the selected metric is computed; switching to another metric starts another job and the result is
   cached in the comparison until a slot changes.

All of it runs on a background thread, through OpenCV: on the GPU through OpenCL when a device passed the
start-up checks, otherwise on the CPU with OpenCV's SIMD code and thread pool (`threading.max_workers`). The
main thread keeps drawing and stays responsive, and the status bar shows the progress of the job.

## Where the GPU comes in

The GPU has two separate jobs.

**Colouring the map, always.** The heat map is uploaded once as a single-channel float texture. The
fragment shader normalises it (`value / max`), applies the gamma curve, looks the result up in the
colour-map table and blends it over the *After* image with the intensity, threshold and
proportional-opacity settings, and applies the split mask. Every slider therefore changes the picture in the
next frame without recomputing anything. This is OpenGL 3.3 and does not depend on OpenCL.

**Computing the maps and statistics, when it can be trusted.** The comparison pipeline is written against
OpenCV's `cv::UMat` (the *Transparent API*): the same code runs through OpenCL on the GPU or on the CPU,
depending on a per-thread switch. OpenCV provides the resize, the Gaussian filter and the reductions; the
steps it has no function for – the colour-difference metrics, the SSIM combination, the linear-light
conversions – are small OpenCL C kernels in `src/core/cl_kernels.h`, each with a CPU twin in
`src/core/cv_ops.cpp` that computes the same thing.

Version 0.2 kept all of this on the CPU on purpose: on a Windows handheld an OpenCL runtime was an extra
install and a driver risk, and the CPU was fast enough. The macOS port changes both sides of that trade.
OpenCL ships with every macOS 12 system, so there is nothing to install, while the target MacBook Air has
only two CPU cores next to an integrated GPU. The driver risk remains – Apple's OpenCL has not been
updated for years – so it is handled explicitly rather than avoided:

- in `auto` mode the kernels must reproduce the CPU results on a test pattern at start-up, or the app stays
  on the CPU (`acceleration.opencl`, [how to use the GPU](../how-to/use-the-gpu.md));
- a CPU-only OpenCL device is not used in `auto` mode, because OpenCV's own CPU code is faster;
- images below `acceleration.min_gpu_pixels` stay on the CPU, where the copy to and from the GPU would cost
  more than it saves;
- the CPU path is always complete, so the app never depends on OpenCL.

On the CPU path, measured on a two-core Linux virtual machine: resampling a 12-megapixel original to
1080 px takes about 0.16 s, a ΔE2000 heat map at 1080 × 810 about 0.1 s and the statistics about 0.14 s; a
ΔE2000 map at the full 4000 × 3000 takes about 1.4 s. How much the GPU of a given Mac adds is measured with
`cc_bench`; figures for the target MacBook Air are not available yet. CLIJ2's kernels were the model for
the implementation, because they are small, well specified and map one output pixel to one work-item.

## Power management

The main loop uses `glfwWaitEventsTimeout`: it sleeps until an event arrives or the timeout passes, at
`app.frame_timing.idle_interval_s` (4 Hz) when nothing is happening and `fast_interval_s` (60 Hz) while a
job runs, a drag is in progress, or for `idle_after_interaction_s` after the last input. Minimised, it
sleeps `iconified_sleep_ms` per iteration and does not draw. Textures are uploaded only when a comparison
or a slot changes, so a frame is a handful of draw calls; thumbnails are limited to
`limits.thumbnail_max_px`. When nothing happens the app idles at a few frames per second, which keeps a
MacBook Air cool and its fan quiet.

## Accuracy caveats

- **Decoders differ.** Stills are decoded by the libjpeg-turbo, libpng, libwebp and libtiff copies bundled
  with the pinned OpenCV, so the macOS and Linux builds from `scripts/build-deps.sh` agree. A build against a
  distribution's OpenCV, or the site's own decoder, can differ by ±1 level in a few pixels because of IDCT
  rounding and chroma upsampling choices – far below any visible threshold, but enough that *untouched
  pixels* need not be exactly 100 % for an unchanged JPEG decoded two ways.
- **Orientation and colour space are applied on decoding.** JPEGs are decoded with their EXIF orientation
  applied, and HEIC photos (ImageIO, macOS) are rotated and converted to sRGB, because that is what a site
  does before re-encoding. An original whose orientation tag the site ignored will therefore not line up.
- **GPU and CPU agree within a tolerance, not bit for bit.** Float rounding differs between an OpenCL
  driver and the CPU; the self-test accepts differences up to `acceleration.self_test_tolerance` (0.01) per
  value, so the statistics of the two paths can differ in the last decimals.
- **Videos are decoded by the video back-end** – AVFoundation on macOS, the FFmpeg libraries on Linux, or
  the `ffmpeg` executable as fallback – including the YUV→RGB conversion (BT.601 vs BT.709 matrices, limited
  vs full range) and the seek to the requested timestamp, so the back-ends can differ slightly. Two clips
  whose timelines were shifted by the site compare offset frames, and the plots will say so loudly.
- **JPEG quality is an estimate** obtained by inverting libjpeg's scaling formula; encoders with their own
  tables are flagged as *custom tables* and the number is then only a rough indication.
- **Site notes** describe typical behaviour observed in 2026; pipelines change without notice. The
  measured numbers are the truth, the notes are context.
- **ΔE2000 assumes sRGB** for both files. A wide-gamut original that a site converted to sRGB will show a
  colour error that is the conversion, not the compression.
