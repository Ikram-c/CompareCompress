# How to use the GPU (OpenCL)

Resampling, every heat map and the statistics can run on the GPU through OpenCL, by way of OpenCV's
Transparent API. The same work runs on the CPU (OpenCV's SIMD code on all cores) when OpenCL is off or not
usable, and the results are the same within a small tolerance. This page shows how to find out what the app
is using, how to change it, and how to measure whether the GPU is worth it on your Mac.

## Check what is in use

From Terminal:

```
dist/CompressCompare.app/Contents/MacOS/CompressCompare --system-info
```

prints, for example:

```
CompressCompare 0.3.0
OpenCV:         4.12.0 (2 threads, SSE4.2 AVX AVX2 FMA3)
OpenCL device:  Intel(R) HD Graphics 6000 - OpenCL 1.2 (Apple)
OpenCL in use:  yes
Video back-end: AVFOUNDATION (acceleration.video_backend: auto)
Still images:   OpenCV imgcodecs (JPEG, PNG, WebP, GIF, BMP, TIFF), macOS ImageIO (HEIC, HEIF, AVIF on macOS 13+)
```

(The device line shows what OpenCV reports for your Mac; the names above are an illustration.) When OpenCL is
not in use the line says why, for example `OpenCL in use:  no - self-test failed: 2.50 % of the GPU results
differ from the CPU` or `no - switched off (acceleration.opencl: off)`.

Every normal start prints the same decision as one line on the console: `OpenCV 4.12.0, OpenCL: <device>` or
`OpenCV 4.12.0, OpenCL: off (<reason>)`.

In the app, open **Settings** (`Cmd+P` on macOS, `Ctrl+P` on Linux). The section **Acceleration
(OpenCV / OpenCL)** shows the OpenCV version, the CPU threads and SIMD features, the OpenCL device, the video
back-end and the still-image decoders, and has a checkbox **use the GPU (OpenCL) for heat maps, statistics
and resampling**. The checkbox is greyed out, with the reason underneath, when OpenCL could not be brought
up.

## What happens at start-up

`init_acceleration()` (in `src/core/accel.cpp`) runs once, before any image work:

1. It sets the size of OpenCV's thread pool to `threading.max_workers`, capped by the number of CPU cores.
2. With `acceleration.opencl: off` it stops here: CPU only.
3. If `acceleration.opencl_device` is set, it is passed to OpenCV as `OPENCV_OPENCL_DEVICE` (see below).
4. It asks OpenCV for an OpenCL runtime and the default device. No runtime or no usable device: CPU only.
5. In `auto` mode, a device that is itself a CPU is not used, because OpenCV's own CPU code is faster than
   an OpenCL driver running on the same cores.
6. It builds the app's kernels (`src/core/cl_kernels.h`, OpenCL C 1.2). A build failure: CPU only.
7. In `auto` mode, the **self-test** runs every kernel on a small synthetic pattern (saturated colours,
   greys, gradients, partial alpha) on the GPU and on the CPU and compares the results value by value. A value
   counts as different when it differs by more than `acceleration.self_test_tolerance` (0.01); if more than
   0.1 % of the values differ, the driver is not trusted and the app uses the CPU.

The self-test exists because an old or faulty OpenCL driver does not usually fail loudly: it returns
plausible numbers that are wrong, and a wrong heat map looks just like a right one.

## Choose the mode

In an override `config.yaml` ([how to change a setting](change-config.md)):

```yaml
acceleration:
  opencl: auto        # auto | on | off
```

| Value | Behaviour |
|---|---|
| `auto` (default) | use a GPU device whose kernels build and pass the self-test; never a CPU-only OpenCL device |
| `on` | use any OpenCL device whose kernels build, CPU devices included, without the self-test |
| `off` | never use OpenCL |

The Settings checkbox switches OpenCL on and off for the rest of the session without touching any file; it
affects work started afterwards (the next heat map or comparison), and it can only switch on what start-up
brought up.

To pick a device other than OpenCV's default, set a selector in OpenCV's `OPENCV_OPENCL_DEVICE` syntax
(`<platform>:<type>:<device>`):

```yaml
acceleration:
  opencl_device: ":GPU:0"      # the first GPU; ":CPU:" would select a CPU device
```

An empty string (the default) leaves the choice to OpenCV.

## Small images stay on the CPU

Copying an image to the GPU and the result back costs time of its own, which is only earned back on large
images. Each operation therefore checks the size of the image it works on (for resampling, the larger of the
source and the target) against `acceleration.min_gpu_pixels` (262 144, i.e. 512 × 512) and runs on the CPU
below it. Set it to `0` to send everything to the GPU, or raise it if measurements show that the GPU only
pays off for larger images.

## Measure it with cc_bench

`cc_bench` is built with the app (`build/macos/cc_bench`, or `build/linux/cc_bench`). Run it from the project
folder, optionally with an image of your own:

```
build/macos/cc_bench                         # uses samples/original.png
build/macos/cc_bench ~/Pictures/photo.jpg
```

It scales the image to a 4000 × 3000 (12-megapixel) "phone photo", makes a 1080 × 810 JPEG q70 "site" copy,
and times each step first with OpenCL off, then with OpenCL on when a device is available: the resample, the
cover-fit to the comparison size, each of the six heat maps, the statistics, and a ΔE2000 heat map at the
full 4000 × 3000. For the GPU pass it also prints the self-test result and the number of kernel launches.
`cc_bench` forces OpenCL on and sets `min_gpu_pixels` to 0, so it measures the device even where `auto`
would reject it.

For comparison, the CPU path on a two-core Linux virtual machine measured about 0.16 s for the
12 MP → 1080 px resample, about 0.1 s for a ΔE2000 heat map at 1080 × 810, about 0.14 s for the statistics
and about 1.4 s for ΔE2000 at 4000 × 3000. GPU figures for the target MacBook Air have not been measured
yet; run `cc_bench` on your Mac and choose `acceleration.opencl` and `min_gpu_pixels` from its output.

## What runs where

| Work | With OpenCL enabled | Without |
|---|---|---|
| decoding stills (OpenCV imgcodecs, macOS ImageIO) | CPU | CPU |
| decoding video frames | AVFoundation on macOS (hardware H.264/HEVC decoder), FFmpeg libraries on Linux, or the `ffmpeg` executable | same |
| sRGB ↔ linear-light conversion and `cv::resize` in `resample()` | GPU (custom kernels and OpenCV's built-in resize) | CPU |
| per-pixel metrics: absolute, luma and squared difference, ΔE76, ΔE2000 | GPU (custom kernels) | CPU twin in `src/core/cv_ops.cpp`, rows split over OpenCV's thread pool |
| SSIM: luma, Gaussian moments, combination | GPU (`luma_pair` and `ssim_combine` kernels, `cv::GaussianBlur`) | CPU |
| statistics: MSE, means, maximum ΔE2000, share over the JND, untouched pixels | GPU (`cv::mean`, `cv::minMaxLoc`, `cv::compare`, `cv::countNonZero`) | CPU |
| heat-map percentiles and histogram, block view | CPU (after the map is copied back) | CPU |
| colouring the heat map, overlay, split, zoom | GPU, through the OpenGL 3.3 shader | same |

The colouring on screen always uses OpenGL, independent of these settings.

## Related

- [config.yaml reference](../reference/config.md) – the `acceleration` and `threading` keys.
- [How a comparison works](../explanation/how-it-works.md) – why the computation can run on either side.
- [Run the tests](run-tests-and-static-analysis.md) – `test_opencl` compares the GPU and CPU results for
  every metric, the statistics and the resampler.
