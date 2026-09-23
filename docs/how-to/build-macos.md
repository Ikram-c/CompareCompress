# How to build on macOS

This port targets **macOS 12 Monterey or newer on Intel (x86_64)**; it was designed around a 2015–2017
MacBook Air (dual-core i5, Intel HD Graphics 6000, 8 GB). There is no prebuilt app: you build it on the Mac
that will run it, which takes one command and, the first time, about half an hour.

What the build needs:

- the **Xcode Command Line Tools** (Apple clang, `git`, `make`, `curl`, the macOS SDK with libcurl and the
  OpenCL, AVFoundation and ImageIO frameworks). The full Xcode app is not needed;
- an **internet connection** for the first build (OpenCV, Dear ImGui, GLFW, yaml-cpp and, if needed, CMake
  and NASM are downloaded).

Homebrew no longer supports macOS 12 (it is Tier 3 there), so the build does not use it. Nothing has to be
installed from Homebrew or MacPorts; MacPorts is only a convenient source of the optional `ffmpeg`
([how to install ffmpeg](install-ffmpeg.md)).

## The one-command build

1. Install the Command Line Tools once, if you have not already:
   ```
   xcode-select --install
   ```
2. In Terminal, from the project folder:
   ```
   scripts/build-macos.sh
   ```

The script prints a `==>` line for each step:

1. checks that it runs on macOS, warns below macOS 12, and stops if the Command Line Tools are missing;
2. uses the installed `cmake` if it is 3.21 or newer, otherwise downloads CMake 3.30.5 into
   `deps/cmake/` (nothing is installed system-wide);
3. runs `scripts/build-deps.sh`, which builds a static OpenCV 4.12.0 into `deps/opencv/` (skipped when it
   is already there);
4. configures with the `macos-intel` preset (`build/macos/`, Unix Makefiles, Release, deployment target
   12.0, x86_64) and builds;
5. runs the tests with `ctest`, including the GPU-against-CPU comparison on this Mac's OpenCL driver;
6. installs to `dist/CompressCompare.app`, signs it ad hoc (`codesign --sign -`), and prints the
   `--system-info` report: OpenCV version, CPU threads and SIMD features, the OpenCL device and whether it is
   in use, the video back-end and the still-image decoders.

Options:

| Option | Effect |
|---|---|
| `--no-tests` | skip step 5 |
| `--clean` | delete `build/macos/` first; the OpenCV build in `deps/` is kept |
| `--help` | print the usage text |

The first run spends most of its time compiling OpenCV: about 20–30 minutes on a dual-core MacBook Air
(about 12 minutes were measured on a two-core Linux virtual machine). Later runs reuse `deps/opencv/` and
take a few minutes.

## Run it

```
open dist/CompressCompare.app
```

or double-click it in Finder, or drag it to `/Applications` first. To start it with files, give absolute
paths after `--args` (an app started with `open` does not run in your current folder):

```
open -a "$PWD/dist/CompressCompare.app" --args "$PWD/samples/original.png" "$PWD/samples/instagram.jpg"
```

The executable inside the bundle also runs directly from Terminal, which is the way to see its console output
and to use the command-line switches:

```
dist/CompressCompare.app/Contents/MacOS/CompressCompare --system-info
dist/CompressCompare.app/Contents/MacOS/CompressCompare samples/original.png samples/instagram.jpg
```

## The same build, step by step

What the script does, in commands you can run yourself (for example to use the Debug preset or to rebuild
only the app):

```
scripts/build-deps.sh                          # OpenCV 4.12.0 into deps/opencv (once)
cmake --preset macos-intel                     # configure into build/macos
cmake --build --preset macos-intel             # build (4 jobs)
ctest --preset macos-intel                     # 9 tests
cmake --install build/macos --prefix dist      # dist/CompressCompare.app, docs, licences
codesign --force --deep --sign - dist/CompressCompare.app
```

The build tree then contains `build/macos/CompressCompare.app`, the `test_*` programs and `cc_bench` (the
benchmark described in [How to use the GPU](use-the-gpu.md)). The install step also copies `README.md`,
`THIRD_PARTY_NOTICES.md`, `LICENSE` and `docs/` into `dist/`.

| Preset | Build type | Build directory |
|---|---|---|
| `macos-intel` | Release | `build/macos` |
| `macos-intel-debug` | Debug | `build/macos-debug` (no test preset: use `ctest --test-dir build/macos-debug`) |

The presets are only offered on a Mac (they are conditioned on the host system); on Linux use
`linux-system` ([Build on Linux](build-linux.md)).

### What `scripts/build-deps.sh` builds

A static OpenCV 4.12.0, cloned from GitHub into `deps/src/`, built in `deps/build/` and installed in
`deps/opencv/`:

- only the modules the app uses: `core`, `imgproc`, `imgcodecs`, `videoio`;
- image codecs from OpenCV's own bundled sources (libjpeg-turbo, libpng, libwebp, libtiff, zlib, plus its
  GIF and BMP readers), so no system image libraries are involved;
- OpenCL on, Intel IPP (the free ICV subset) on by default, no TBB, OpenMP, Eigen or LAPACK;
- on macOS: deployment target 12.0, x86_64, AVFoundation for video, no FFmpeg. On Linux: the FFmpeg
  libraries for video.

It accepts these environment variables:

| Variable | Default | Effect |
|---|---|---|
| `JOBS` | number of CPUs | parallel compile jobs (`JOBS=2 scripts/build-deps.sh` keeps the Mac usable meanwhile) |
| `WITH_IPP` | `ON` | `OFF` builds without Intel IPP |
| `WITH_NASM` | `ON` | `OFF` skips building NASM (see below) |
| `OPENCV_VERSION` | `4.12.0` | the OpenCV tag to build |
| `CC_DEPS_DIR` | `deps` | where sources, build tree and result go |

The script writes `deps/opencv/.built-4.12.0` when it finishes and does nothing on later runs while that file
exists. An interrupted build resumes where it stopped when you run it again.

CMake finds this OpenCV automatically. Any other OpenCV 4.6 or newer with the four modules works too if you
pass `-DOpenCV_DIR=<dir containing OpenCVConfig.cmake>`.

### The other libraries

`cmake/Dependencies.cmake` looks for each small library with `find_package`, then `pkg-config`, and otherwise
fetches it from GitHub and builds it with the app: Dear ImGui v1.92.9b always, GLFW 3.4 and yaml-cpp 0.8.0
when no installed copy is found. libcurl (URL downloads) comes from the macOS SDK. OpenGL is the system
framework; the app asks for a 3.3 core, forward-compatible context (macOS provides 4.1) and is compiled with
`GL_SILENCE_DEPRECATION`, because OpenGL is deprecated on macOS, not removed.

### CMake options

| Option | Default | Effect |
|---|---|---|
| `CC_BUILD_APP` | ON | build the GUI (off = core library, tests and `cc_bench` only, no OpenGL or GLFW) |
| `CC_BUILD_TESTS` | ON | build and register the `test_*` programs |
| `CC_WITH_CURL` | ON | look for libcurl (without it, URL fetching is disabled) |
| `CC_WARNINGS_AS_ERRORS` | ON | `-Werror` |
| `CC_ENABLE_CLANG_TIDY` | OFF | run clang-tidy on every file during the build |
| `OpenCV_DIR` | `deps/opencv/...` | use another OpenCV installation |

## What is in the app bundle

```
CompressCompare.app/Contents/
  Info.plist                 minimum macOS 12.0, high-resolution (Retina) capable, version 0.3.0
  MacOS/CompressCompare      the executable (OpenCV and Dear ImGui linked statically; GLFW and yaml-cpp too when fetched)
  Resources/CompressCompare.icns
```

The built-in configuration is compiled into the executable; the bundle contains no `config.yaml`. Your own
settings go in `~/Library/Application Support/CompressCompare/config.yaml`, which the **edit config.yaml...**
button in the Settings window creates and opens in TextEdit ([how to change a setting](change-config.md)).

## Troubleshooting

**"The Xcode Command Line Tools are missing."** Run `xcode-select --install`, accept the dialog, and start
the script again. After a macOS update, `xcode-select --install` may be needed once more; `xcode-select -p`
prints the active tools folder when they are installed.

**The CMake download fails.** The script fetches
`cmake-3.30.5-macos10.10-universal.tar.gz` from GitHub. Behind a proxy or offline, install CMake 3.21 or newer
yourself (the universal `.dmg` from <https://cmake.org/download/>, then put
`/Applications/CMake.app/Contents/bin` on `PATH`, or `sudo port install cmake`) and run the script again; it
uses any `cmake` of 3.21 or newer it finds on `PATH`.

**OpenCV takes very long, or the Mac gets hot and loud.** That is the first build; 20–30 minutes on a
dual-core Air is normal. `JOBS=2 scripts/build-macos.sh` (or a lower number) keeps the machine responsive at
the cost of time. If it is interrupted, run the script again: it continues the OpenCV build instead of
starting over.

**"warning: NASM could not be built".** libjpeg-turbo's SIMD code needs the NASM assembler, which the
Command Line Tools do not include, so `build-deps.sh` downloads NASM 2.16.03 from nasm.us and builds it into
`deps/tools/`. If that fails, OpenCV still builds with the plain-C JPEG code (JPEG decoding is then two to
three times slower). To get the fast path afterwards, install NASM (`sudo port install nasm`), delete
`deps/opencv/` and `deps/build/` (so that OpenCV's configuration looks for NASM again) and run the script
again.

**The OpenCV configure step fails while downloading IPP.** OpenCV's CMake downloads the Intel IPP ICV
package from GitHub during configuration. If that download fails (proxy, firewall), build without IPP:
```
WITH_IPP=OFF scripts/build-deps.sh
scripts/build-macos.sh
```
IPP speeds up some OpenCV functions on Intel CPUs; everything works without it. To remove IPP from an OpenCV
that was already built, delete `deps/opencv/` and run `WITH_IPP=OFF scripts/build-deps.sh`.

**The `opencl` or `opencl_cpu_device` test fails.** The test compares every GPU result with the CPU result
on this Mac's OpenCL driver. A failure means the driver computes something differently; the app itself is
not affected in the default `acceleration.opencl: auto` mode, because the same comparison runs at start-up
and the app falls back to the CPU when it fails. Build without the test run
(`scripts/build-macos.sh --no-tests`), then look at `--system-info`: *OpenCL in use: no - self-test failed…*
confirms the fallback. Set `acceleration.opencl: off` to skip OpenCL altogether. See
[How to use the GPU](use-the-gpu.md).

**macOS says the app "cannot be opened" or "is damaged".** A build made on the same Mac carries no
quarantine flag and is signed ad hoc by the script, so it opens normally. If you copied the app from another
Mac (AirDrop, a download, a USB stick), Gatekeeper refuses the unsigned copy: Control-click the app, choose
*Open* and confirm once, or remove the flag with
`xattr -dr com.apple.quarantine CompressCompare.app`. If you rebuilt it without the script, sign it with
`codesign --force --deep --sign - dist/CompressCompare.app`.

**Where is my configuration, and where is the window layout?** Both live in
`~/Library/Application Support/CompressCompare/`: `config.yaml` (your overrides; Settings > *edit
config.yaml...* creates it) and
`ui.ini` (Dear ImGui's window positions; delete it to reset the layout). In Finder, press Shift-Cmd-G and
paste the path.

**Building on a Mac with Apple silicon.** It is not a target of this port. The script builds an x86_64 app
there as well and notes that it runs under Rosetta 2.
