# Where the ideas come from

CompressCompare was designed after reading two code bases the brief asked for, and its compare views follow
Adobe Lightroom Classic's documentation. This page maps each borrowed idea to the file, function or
`config.yaml` key where it landed, so that credit is traceable and the origin of a behaviour can be looked
up. Nothing was copied from darktable (GPL-3.0-or-later); its files are cited as *behaviour and algorithm
references* and re-implemented. The CLIJ2 / clEsperanto kernels (BSD-3-Clause) were ported line by line to
C++ in version 0.2; in the macOS port (0.3) their role is taken by OpenCV functions and a few OpenCL kernels
of this project's own, as noted below.

## darktable (`darktable-master`, commit 92f6c28, Sept 2026)

| darktable file | What it taught | Where it landed |
|---|---|---|
| `src/libs/snapshots.c` | Before/after split view: split position kept as a 0..1 fraction of the viewport (`vp_xpointer`/`vp_ypointer`, default 0.5), left/right vs top/bottom via a `vertical` flag, `inverted` swaps sides, click-drag moves the split, a rotate handle in the middle of the line cycles the orientation, the line is 1 px at 70 % alpha, handle highlights when the pointer is near, side-by-side mode. | `src/app/ui_compare.cpp` (`ViewState::split`, `split_invert`, `dragging_split`, the divider/handle drawing and hit-testing), `Layout::LeftRightSplit/TopBottomSplit`, `swap` button, `\` key. |
| `src/libs/live_view.c` | Simpler 4-state split rotation, 5 px / 7 px hit radii for line vs handle, split clamped to [0,1] while dragging, DIFFERENCE/EXCLUSION blend overlays. | Hit radii `view.split.line_hit_px` (7) and `view.split.handle_hit_px` (14) in `config.yaml`, clamping in `ui_compare.cpp`; the difference overlay became the shader's heat-map overlay mode. |
| `src/libs/duplicate.c` | Press-and-hold a version to momentarily see it, release to return. | Hold `Space` to peek at the original in Loupe view. |
| `src/views/view.c` | Both images painted through one viewport transform so they line up pixel for pixel; re-render when a hash of zoom/pan changes. | Every pane uses the same `Placement` (zoom + centre) → `place_image()`; textures only change when a comparison is recomputed. |
| `src/develop/develop.c` | Zoom steps ×1.1 below 200 % and ×2 above, snapping to fit / 100 % / 200 %, floor `min(0.5·fit, 1)`, cap 16×, keeping the image point under the cursor fixed; middle click cycles fit → 100 % → 200 %. | `App::zoom_step()` and `App::toggle_zoom_1_1()` in `src/app/app.cpp`; the steps, cap and floor are `view.zoom.*` in `config.yaml`. |
| `src/dtgtk/culling.c`, `src/dtgtk/thumbnail.c` | Culling/compare layout with all images at equal area, synchronised zoom and pan ("zoom all" applies the same local offset to every thumb; Shift restricts to one), pan clamped to the real extent with small images centred, NEAREST filtering near 1:1, `zoom` 1.0 = fit. | Survey layout (`Layout::Survey`), linked zoom/pan (`ViewState::link_focus`), pan clamping in `place_image()`, `DrawParams::nearest = zoom >= 1`. |
| `src/dtgtk/thumbnail.c`, `src/libs/tools/global_toolbox.c` | Overlay modes "on hover", "extended on hover", "hover block" with a timeout; tooltip text built from file variables; the "fit / NN %" zoom label. | Slot thumbnails show a details overlay on hover (format, JPEG q, subsampling, markers, decoder, path), the toolbar's `fit` / `100%` read-out, the ImGui hover-delay style (`app.tooltip.*` in `config.yaml`). |
| `src/libs/colorpicker.c` | Pixel read-outs as RGB, hex and Lab with colour swatches. | Hover tooltip in the viewer (before/after RGB + hex swatches, ΔRGB, ΔE2000, metric value). |
| `src/common/colorspaces_inline_conversions.h`, `data/kernels/colorspace.h` | sRGB transfer curve constants (0.04045 / 12.92 / 1.055 / 2.4), the D65 sRGB→XYZ matrix (0.4124564 …), the Lab `f()` with ε = 216/24389 and κ = 24389/27. darktable uses a D50 Lab with Bradford adaptation; for two sRGB images D65 Lab is used here. | `srgb8_to_lab()` in `src/core/metrics.cpp` (LUT for the transfer curve, matrix, Lab). |
| `src/chart/deltaE.c` | ΔE76 and ΔE2000 reference implementation (and its missing "h1'+h2' ≥ 360" branch, which is handled here). | `delta_e76()` / `delta_e2000()` following Sharma, Wu & Dalal 2005, validated against their test pairs. |
| `src/imageio/format/jpeg.c`, `src/imageio/imageio_jpeg.c` | How quality maps to chroma subsampling (≤90 → 4:2:0, 91–92 → 4:2:2, ≥93 → 4:4:4), `jpeg_set_quality(force_baseline)`, ICC in APP2, EXIF in APP1; the read side never looks at the DQT tables — the hook point after `jpeg_read_header`. | `src/core/jpeg_info.cpp` parses SOF/DQT/APP markers directly and inverts the IJG `jpeg_quality_scaling()` formula; `image.cpp` decodes through OpenCV's `cv::imdecode` (its bundled libjpeg-turbo) with the EXIF orientation applied. |
| `src/imageio/imageio.c` (signature table) | Detect file type by content, not extension (sites serve WebP as `.jpg`). | `sniff_bytes()` in `src/core/image.cpp` (JPEG/PNG/GIF/BMP/TIFF/WebP/AVIF/HEIF/ISO-BMFF video/Matroska/FLV/MPEG-PS/Ogg/ASF/HTML). |
| `src/imageio/imageio_webp.c`, `format/webp.c` | `WebPDecodeRGBA` read path; lossy quality/`sharp YUV` write defaults. | WebP decoding in `image.cpp` (through OpenCV's bundled libwebp, with the size read from the RIFF header before decoding); the site notes mention WebP conversion. |
| `src/common/curl_tools.c`, `cmake/windows-macros.cmake`, `src/common/ai_models.c` | libcurl set-up: user agent, `FOLLOWLOCATION`, CA bundle handling on Windows, connect timeouts, low-speed abort. | `src/core/net.cpp` (libcurl; on macOS the copy in the system SDK). |
| `src/common/box_filters.cc`, `src/common/fast_guided_filter.h` | O(1) running box mean; packing `{I, p, I², I·p}` moments and box-averaging them once to get local variance/covariance — exactly SSIM's local statistics. | `ssim_map()` in `src/core/metrics.cpp` computes μ, E[a²], E[b²], E[ab] with `cv::GaussianBlur` and derives σ² and the covariance in the `ssim_combine` kernel. |
| `src/common/gaussian.c` | `dt_gaussian_kernel_1d`: radius = round(3σ); σ = 1.5 gives the 11-tap window from Wang et al.'s SSIM paper. | `metrics.ssim.window: 11`, `metrics.ssim.sigma: 1.5` in `config.yaml`, used by `ssim_map()`. |
| `src/common/interpolation.c`, `src/develop/imageop_math.c` | Downscaling must stretch the kernel by 1/scale (area-correct anti-aliasing); gamma-aware scaling (resample in linear light). | `resample()` in `src/core/image.cpp`: sRGB → linear light with premultiplied alpha, `cv::resize` with `INTER_AREA` when shrinking in both directions and `INTER_CUBIC` otherwise, and back. |
| `src/common/focus_peaking.h` | A robust-threshold false-colour overlay composited over the image. | The heat-map overlay with cut-off (`heatmap.threshold`) and "opacity follows error" (`heatmap.proportional`). |
| `src/common/histogram.c`, `src/libs/scopes/histogram.c` | OpenMP-reduced histogram, linear/log toggle. | Error histogram in the Statistics panel (`HeatMap::finalize()`, `metrics.histogram_bins`, `heatmap.log_histogram`). |
| `src/libs/filters/search.c`, `src/libs/modulegroups.c` | darktable only does case-folded substring search, but its pipe-separated **alias list** per module (`"rotation|keystone|crop"`) is a good idea. | The `aliases` list of every site in `config.yaml` (`[ig, insta, gram]`, `[x, twitter, tweet]`), searched by the fuzzy matcher in `src/core/fuzzy.cpp`. |
| `src/common/hdr_alignment.cc` | Feature-based alignment with sanity gates when a site crops. | Not implemented (kept lightweight); a manual crop-offset nudge covers the common centre-crop case. |
| `packaging/windows/README.md`, `src/win/*`, `src/CMakeLists.txt` | MSYS2/MinGW build recipe, `wmain`/UTF-16 argv shims, `_aligned_malloc` pitfalls, `-mwindows` subsystem, DLL bundling. | The Windows builds of version 0.2. This port targets macOS and removed the Windows presets; the UTF-16 file and command-line handling (`_wfopen`, `CommandLineToArgvW`) remains in the source under `#ifdef _WIN32`. |
| `src/common/opencl.c`, `dlopencl.c`, `opencl_drivers_blacklist.h` | Runtime-loaded OpenCL with CPU fallback and a driver blacklist. | Not used in 0.2, where an OpenCL runtime was extra weight on a Windows handheld. Adopted in the macOS port, where OpenCL is part of the system: OpenCV loads the runtime, and instead of a blacklist a start-up self-test compares every kernel with its CPU twin and falls back to the CPU on a mismatch (`src/core/accel.cpp`, `acceleration.*` in `config.yaml`). |

## CLIJ2 / clEsperanto (`clij/clij-opencl-kernels` @ 9c11d9e, `clEsperanto/clij-opencl-kernels` 3.7.0, CLIc 0.25)

CLIJ2 (Haase et al., *Nature Methods* 2020, https://clij.github.io/) is a GPU image-processing library for
ImageJ/Fiji built on small OpenCL kernels. Its C++ successor CLIc wraps the same kernels but pulls in an OpenCL
ICD loader, Eigen and VkFFT — far heavier than this app needs — so in version 0.2 the handful of kernels used
here were ported to plain C++ in `src/core/metrics.cpp`, with identical semantics (one output pixel per
work-item, clamp-to-edge reads, float maths).

The macOS port runs the pipeline through OpenCV instead. Where OpenCV has an equivalent function it is used;
the rest are OpenCL C kernels again (`src/core/cl_kernels.h`), written in CLIJ2's style – one output pixel
per work-item, float maths – but taking OpenCV's `KernelArg` buffers (pointer, step, offset) instead of
CLIJ2's `IMAGE_src_TYPE` / `READ_src_IMAGE` macros, and each with a CPU twin in `src/core/cv_ops.cpp`.

| CLIJ2 kernel / plugin | Now |
|---|---|
| `add_images_weighted_2d_x.cl` with factors (1, −1) → `subtractImages`, then `absolute_2d_x.cl` → **`absoluteDifference`** | `Metric::AbsDiff` (per channel, then the max channel) in the `pixel_metric` and `pair_maps` kernels |
| `subtractImages` + `power_2d_x.cl` (exponent 2) → **`squaredDifference`**; `sum_x/y_projection` → **`meanSquaredError`** = Σ / (w·h) | `Metric::SquaredError` in the kernels; `PairStats::mse` = `cv::mean` of the squared-error map → PSNR |
| `gaussian_blur_separable_2d_x.cl` (weights `exp(v²/(−2σ²))` normalised by their sum, x pass then y pass) | `cv::GaussianBlur` with `BORDER_REPLICATE` (clamp to edge), used with the `metrics.ssim` window (11 taps, σ = 1.5) |
| `mean_separable_2d_x.cl` / `variance_box_2d_x.cl` / `standardDeviationBox` (mean, then Σ(v−μ)²/count) | SSIM's local mean/variance/covariance from the Gaussian-filtered moment images and the `ssim_combine` kernel (`ssim_map()`); block means over `metrics.block_size_px` on the CPU (`block_average()`) |
| `minimum/maximum/mean_of_all_pixels` (z → y → x projections, first element initialises) | `cv::mean` and `cv::minMaxLoc` for the statistics; `HeatMap::finalize()` (min, max, mean, percentiles) on the CPU for the map |
| `greater_or_equal_constant` → `threshold` | Shader cut-off (`u_threshold`) and the JND count (`pct_over_jnd`: `cv::compare` of ΔE2000 against `metrics.jnd_delta_e2000`, then `cv::countNonZero`) |
| `convert_float` / `copy` | 8-bit → float luma in the `luma_pair` kernel |
| Preamble macro conventions: clamp-to-edge reads, out-of-range writes dropped, integer outputs clamped/truncated | `BORDER_REPLICATE` in the filters; kernels return early outside the image; float maps throughout |

Attribution: kernels © 2019 Robert Haase, Nico Stuurman, Deborah Schmidt, Uwe Schmidt, Martin Weigert,
Peter Haub, Fabrice P. Cordelières (MPI-CBG Dresden, University of Virginia, Regents of the University of
California), BSD-3-Clause; `gaussian_blur_separable` adapted from Uwe Schmidt (ClearControl/FastFuse).

## Adobe Lightroom Classic

Compare-view behaviour follows Lightroom Classic's documentation: Develop-module Before/After views
(`Y` Left/Right, `Alt+Y` Top/Bottom, `Shift+Y` Split, `\` Before-only toggle, swap), the Library **Compare**
view with a *Select* and a *Candidate*, **Link Focus** for synchronised zoom/pan, `Z` zoom toggle, and the
**Survey** view of several images at once; `Tab` hides the side panels.

## The metrics themselves

- Wang, Bovik, Sheikh & Simoncelli, *Image quality assessment: from error visibility to structural similarity*,
  IEEE Transactions on Image Processing 13(4), 2004 – SSIM and its 11×11, σ = 1.5 Gaussian window.
- Sharma, Wu & Dalal, *The CIEDE2000 color-difference formula: implementation notes, supplementary test data,
  and mathematical observations*, Color Research & Application 30(1), 2005 – the ΔE2000 formulation and the
  test pairs `tests/test_metrics.cpp` checks against.
- IEC 61966-2-1:1999 (sRGB), ITU-R BT.601 (luma coefficients), CIE 15:2004 (Lab).
- Independent JPEG Group, `jcparam.c` – `jpeg_quality_scaling()` and the Annex K quantisation tables.
- Haase et al., *CLIJ: GPU-accelerated image processing for everyone*, Nature Methods 17, 2020.

## The engineering rules

- G. J. Holzmann, *The Power of 10: Rules for Developing Safety-Critical Code*, IEEE Computer 39(6), 2006 –
  adapted as described in [Design principles](design-principles.md).
- D. Procida, *Diátaxis* (<https://diataxis.fr/>) – the structure of this documentation.
- OpenCV (with its bundled libjpeg-turbo, libpng, libwebp, libtiff and zlib), Dear ImGui, GLFW, yaml-cpp,
  libcurl – see `THIRD_PARTY_NOTICES.md` for the licences.
