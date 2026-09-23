# How to run the tests and the static analysis

The project treats its static-analysis tools as part of the build (Power of 10, rule 10): the compiler
warnings are errors, clang-tidy and cppcheck are expected to report nothing, every function is at most 60
lines, and eight headless test programs cover the core library. This page shows how to run each of them.

The examples use the macOS preset `macos-intel` (build directory `build/macos`) or the Linux preset
`linux-system` (`build/linux`); substitute the one you built with ([macOS](build-macos.md),
[Linux](build-linux.md)).

## Unit tests

They need no window and no network; the OpenCL test uses a GPU when there is one and skips its GPU part when
there is none. After configuring with `CC_BUILD_TESTS=ON` (the default):

```
cmake --build --preset macos-intel
ctest --preset macos-intel                    # or: ctest --preset linux-system
```

or from a build directory: `ctest --test-dir build/macos --output-on-failure`. `scripts/build-macos.sh` runs
them as part of the build. Each test is a plain executable you can also run directly
(`build/macos/test_opencl`); it prints one line per check and exits non-zero if any check failed.

| Test | What it covers |
|---|---|
| `config` | the built-in `config.yaml` parses; override files merge (scalars, nested maps, lists, sites by name); every kind of mistake (unknown key, out-of-range value, wrong type, bad enum, duplicate site, wrong schema version) is reported with the file and the key path; `find_config_override` picks the right file |
| `fuzzy` | the subsequence matcher: exact prefix, gaps, case folding, truncation at the length limits, and that `ig`, `twtr`, `fb`, `bsky`… rank the expected site first |
| `metrics` | ΔE2000 against the published Sharma–Wu–Dalal test pairs, sRGB→Lab, SSIM and PSNR on synthetic images, heat-map percentiles, block averaging, statistics |
| `jpeg_info` | the DQT/SOF/APP parser on JPEGs encoded with OpenCV (libjpeg-turbo uses the IJG tables and scaling, so the quality estimate must be exact), subsampling detection, metadata flags, format sniffing, resampling, the edge-size limit |
| `ffprobe` | parsing of `ffprobe` output (streams, rotation, duration); when `ffmpeg` is installed, an end-to-end round trip: generate a clip, probe it, extract a frame, including a rotated clip. Without ffmpeg those parts print *skipping* and pass |
| `sites` | URL helpers (host, file extension) and the embedded site database: sites are detected from CDN hosts, aliases resolve, the last entry is *Other / custom* |
| `opencl` | the resampler's properties (a black-and-white checkerboard shrunk 4:1 must average to 188, the linear-light mean, not 128); then, when an OpenCL device can be used, the kernel self-test and a comparison of GPU and CPU results for every heat-map metric, the pair statistics and the resampler on a JPEG-compressed test pair. Without a device the GPU part prints *GPU checks skipped* and passes |
| `video` | four parts: the MP4/MOV track-header parser on synthetic boxes (`tkhd` rotation); probing, seeking and scaling the sample clips through OpenCV's video back-end, and that `isobmff_file_video_track()` reads `samples/original.mp4` as `avc1` with rotation 0; when the `ffmpeg` executable is installed, a frame cross-check against it; and a clip given a 90° display rotation, checked on both back-ends. The ffmpeg parts print *skipped* and pass when it is missing |
| `opencl_cpu_device` | `test_opencl` again, with `OPENCV_OPENCL_DEVICE=:CPU:`, so that the kernels are also checked on a CPU OpenCL device (Apple's on a Mac, PoCL on Linux CI) |

`test_opencl` switches OpenCL on regardless of `acceleration.opencl`, so on a Mac it checks exactly the driver
the app would use. If it fails there, see [How to use the GPU](use-the-gpu.md): the app's start-up self-test
runs the same comparison and falls back to the CPU.

## Compiler warnings

Every target compiles with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wcast-qual
-Wformat=2 -Wundef -Wdouble-promotion -Wnull-dereference -Wnon-virtual-dtor -Wold-style-cast
-Wimplicit-fallthrough -Werror` (Apple clang, clang and GCC). Third-party headers, OpenCV's included, are
marked as system headers so their warnings do not count. To turn the errors back into warnings while you
experiment:

```
cmake --preset macos-intel -DCC_WARNINGS_AS_ERRORS=OFF
```

## clang-tidy

The checks are in `.clang-tidy` at the project root (bugprone, cert, clang-analyzer, core-guideline casts,
`misc-no-recursion`, performance, readability incl. magic numbers and function size). The Xcode Command Line
Tools do not include clang-tidy, so this check is normally run on the Linux build. Two ways to run it:

- **During the build** – `cmake --preset linux-system -DCC_ENABLE_CLANG_TIDY=ON`, then build. Every
  translation unit is analysed as it compiles; `WarningsAsErrors: '*'` makes any finding fail the build.
- **Stand-alone**, using the compilation database the configure step writes:
  ```
  run-clang-tidy -p build/linux src
  ```

Expected output: no warnings. Needs clang-tidy 17 or newer (`sudo apt install clang-tidy`).

## cppcheck

```
cmake --build --preset linux-system --target cppcheck
```

runs `cppcheck --enable=warning,style,performance,portability --error-exitcode=1` over `src/`. The target
exists only when cppcheck is installed (`sudo apt install cppcheck`; on a Mac `sudo port install cppcheck`).
Two findings are suppressed on purpose and say so in `CMakeLists.txt`: `useStlAlgorithm` (explicit bounded
loops are preferred, rule 2) and `knownConditionTrueFalse` in `config.cpp` (`return r.fail(...)` always
returns false; the message is the point).

## Function length

```
python3 tools/check_function_length.py        # limit 60 lines
python3 tools/check_function_length.py 40     # any other limit
```

lists every function body longer than the limit with its file, line and signature and exits 1 if there is
one. The same limit is enforced by clang-tidy's `readability-function-size` (60 lines, 120 statements, 8
parameters, nesting 5). Expected output: `functions over 60 lines: 0`.

## Generated pages

Two reference pages are generated from the sources and must be regenerated after a change:

```
python3 tools/gen_config_reference.py   # docs/reference/config.md from config.yaml + config.cpp
python3 tools/gen_site_reference.py     # docs/reference/sites.md from the sites list
```

The first script fails if a key exists in `config.yaml` without a getter in `config.cpp` or the other way
round, which makes it a cheap consistency check as well.

## Benchmark

`cc_bench` (built with the app, `build/macos/cc_bench`) times resampling, every heat map and the statistics
on a 12-megapixel pair, on the CPU and through OpenCL. It is not part of the gate; see
[How to use the GPU](use-the-gpu.md#measure-it-with-cc_bench).

## The whole gate, in order

```
cmake --preset linux-system -DCC_ENABLE_CLANG_TIDY=ON
cmake --build --preset linux-system
ctest --preset linux-system
cmake --build --preset linux-system --target cppcheck
python3 tools/check_function_length.py
python3 tools/gen_config_reference.py && python3 tools/gen_site_reference.py && git diff --exit-code docs/reference
build/linux/compresscompare --check-config
```

Everything green means: no warning, no clang-tidy or cppcheck finding, 9/9 tests, no function over 60 lines,
reference pages in step with the code, and the embedded configuration valid. On the Mac,
`scripts/build-macos.sh` covers the warnings and the tests, on the real OpenCL driver.

## Smoke-testing the GUI

`compresscompare --exit-after 3 samples/original.png samples/instagram.jpg` opens the window, loads the
files, runs the comparison and closes itself after three seconds. On a Mac, run
`build/macos/CompressCompare.app/Contents/MacOS/CompressCompare --exit-after 3 samples/original.png
samples/instagram.jpg` from the project folder. Under `xvfb-run` on a headless Linux box (Mesa's software
renderer provides OpenGL 3.3) this catches start-up and rendering regressions. The exit status is 0 unless
something failed to initialise; if any `CC_REQUIRE` / `CC_ENSURE` contract was violated during the run, a line
`N contract violation(s) were recorded during this session` is printed on stderr.
