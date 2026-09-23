# Third-party notices

CompressCompare's own code is MIT-licensed (see `LICENSE`). It links or ports the following:

| Component | Licence | Use |
|---|---|---|
| [OpenCV](https://opencv.org/) 4.12.0 (modules `core`, `imgproc`, `imgcodecs`, `videoio`) | Apache-2.0 | image decoding and PNG export (`cv::imdecode`, `cv::imencode`), resampling, Gaussian filtering and reductions, the OpenCL Transparent API that runs the kernels, video decoding through `cv::VideoCapture`. Built from source by `scripts/build-deps.sh` and linked statically. |
| ↳ [libjpeg-turbo](https://libjpeg-turbo.org/), bundled with OpenCV | IJG + BSD-3-Clause + zlib | JPEG decoding (inside OpenCV) |
| ↳ [libpng](http://www.libpng.org/pub/png/libpng.html), bundled with OpenCV | libpng licence (PNG Reference Library License v2) | PNG decoding and export (inside OpenCV) |
| ↳ [libwebp](https://developers.google.com/speed/webp), bundled with OpenCV | BSD-3-Clause | WebP decoding (inside OpenCV) |
| ↳ [libtiff](http://www.simplesystems.org/libtiff/), bundled with OpenCV | libtiff licence (BSD-style) | TIFF decoding (inside OpenCV) |
| ↳ [zlib](https://zlib.net/), bundled with OpenCV | zlib | compression for PNG and TIFF (inside OpenCV) |
| ↳ Intel IPP ICV (Integrated Performance Primitives, OpenCV subset), when OpenCV is built with `WITH_IPP=ON` (the default of `scripts/build-deps.sh`) | Intel Simplified Software License (Version October 2022) | optimised implementations of some OpenCV functions on Intel CPUs; linked statically. Build with `WITH_IPP=OFF` to leave it out. |
| ↳ [FFmpeg](https://ffmpeg.org/) libraries (Linux builds only) | LGPL-2.1+ or GPL, depending on how the distribution built them | OpenCV's video back-end on Linux (`libavcodec`, `libavformat`, `libavutil`, `libswscale`, linked dynamically from the system). Not used on macOS. |
| [Dear ImGui](https://github.com/ocornut/imgui) 1.92.9b | MIT | UI, GLFW + OpenGL3 backends |
| [GLFW](https://www.glfw.org/) 3.4 | zlib/libpng | window, input, OpenGL context, drag-and-drop, clipboard |
| [yaml-cpp](https://github.com/jbeder/yaml-cpp) 0.8 | MIT | parsing `config.yaml` (the embedded defaults and override files) |
| [libcurl](https://curl.se/) (optional) | curl licence (MIT-style) | URL fetching; on macOS the copy in the system SDK |
| [FFmpeg](https://ffmpeg.org/) `ffmpeg`/`ffprobe` (optional) | LGPL-2.1+/GPL depending on build | **not linked** — invoked as external executables, when installed, as the fallback for videos the OpenCV back-end cannot open and for stills the built-in decoders cannot read (AVIF and HEIC on Linux, AVIF on macOS 12) |
| macOS system frameworks: ImageIO, CoreGraphics, CoreFoundation, AVFoundation (through OpenCV), OpenCL, OpenGL, Cocoa | part of macOS | HEIC/HEIF/AVIF decoding, video decoding, GPU compute, window and drawing. Linked from the system, not redistributed. |
| [CLIJ2 / clEsperanto OpenCL kernels](https://github.com/clij/clij-opencl-kernels) | BSD-3-Clause | model for the metric kernels: ported to C++ in version 0.2; in 0.3 the per-pixel kernels in `src/core/cl_kernels.h` and their CPU twins in `src/core/cv_ops.cpp` follow the same structure (absolute/squared difference, MSE, box and Gaussian moment statistics, reductions). Copyright 2019 Robert Haase, Nico Stuurman, Deborah Schmidt, Uwe Schmidt, Martin Weigert, Peter Haub, Fabrice P. Cordelières; Max Planck Institute for Molecular Cell Biology and Genetics Dresden, University of Virginia, Regents of the University of California. The 0.2 port of `gaussian_blur_separable` was adapted from Uwe Schmidt, ClearControl/FastFuse; 0.3 uses OpenCV's `cv::GaussianBlur` instead. |
| [darktable](https://www.darktable.org/) | GPL-3.0-or-later | **reference only** — UI behaviours (snapshots split view, culling zoom/pan, thumbnail hover overlays), colour maths and I/O conventions were studied and re-implemented; no darktable code is included. See `docs/explanation/references.md`. |
| matplotlib colour maps (viridis, inferno anchor colours) | CC0 / public domain | heat-map palettes |
| [Adobe Lightroom Classic](https://helpx.adobe.com/lightroom-classic/) | – | **reference only** – the Before/After, Compare, Survey and Link Focus behaviours and their key bindings were modelled on its documentation; no Adobe code or assets are involved. |

OpenCV installs the licence texts of what it bundles – including Berkeley SoftFloat (BSD-3-Clause), the
Khronos OpenCL headers and FlatBuffers used inside OpenCV's core, and the IPP licence and third-party notices –
in `deps/opencv/share/licenses/opencv4/`; OpenCV's own licence is in `deps/src/opencv-4.12.0/LICENSE`.

Only tools, not part of the app: CMake (downloaded into `deps/` by `scripts/build-macos.sh` when missing),
NASM (built into `deps/tools/` by `scripts/build-deps.sh` to assemble libjpeg-turbo's SIMD code), the Xcode
Command Line Tools.

Earlier versions (0.2 and before) linked stb (`stb_image`, `stb_image_write`, `stb_image_resize2`),
libjpeg-turbo through the TurboJPEG API and libwebp directly. This port no longer uses stb; libjpeg-turbo,
libwebp and the other codecs are now reached only through OpenCV, as listed above.

The CIEDE2000 implementation follows G. Sharma, W. Wu, E. N. Dalal, "The CIEDE2000 color-difference formula:
Implementation notes, supplementary test data, and mathematical observations", Color Res. Appl. 30 (2005).
SSIM follows Z. Wang, A. C. Bovik, H. R. Sheikh, E. P. Simoncelli, "Image quality assessment: from error
visibility to structural similarity", IEEE TIP 13 (2004). JPEG quality estimation inverts the IJG libjpeg
`jpeg_quality_scaling()` function (jcparam.c) against the ITU-T T.81 Annex K tables.

The Power of 10 rules the code follows are from G. J. Holzmann, "The Power of 10: Rules for Developing
Safety-Critical Code", IEEE Computer 39 (2006); the documentation structure follows Daniele Procida's
Diátaxis (https://diataxis.fr/).
