# Command-line reference

```
compresscompare [--config FILE] [--exit-after SECONDS] [file ...]
compresscompare --check-config | --system-info [--config FILE]
compresscompare --help | --version
```

On macOS the program is the executable inside the app bundle,
`CompressCompare.app/Contents/MacOS/CompressCompare`; run it from Terminal to use these options and see the
console output, or pass arguments through `open`:

```
open -a /Applications/CompressCompare.app --args "$PWD/original.png" "$PWD/instagram.jpg"
```

An app started with `open` or from Finder does not run in your current folder, so give absolute paths there.
On Linux the program is `compresscompare` (`build/linux/compresscompare` after a build).

Options and files may appear in any order. An argument that starts with `-` and is not listed below is an
error (`unknown option …`); a lone `-` is treated as a file name. An argument starting with `-psn_` (the
process serial number some macOS versions pass to an app started from Finder) is ignored.

## Positional arguments

| Argument | Meaning |
|---|---|
| `file …` | Files (or `http://` / `https://` URLs) loaded into the slots in order: the first fills the **Original** slot, each further one fills the next site slot, adding slots as needed up to `limits.max_site_slots` (16); at most `limits.max_dropped_files` (64) are taken. Loading happens as if the files had been dropped on the window, so a video, a URL or an unreadable file is reported in the slot, not on the console. |

## Options

| Option | Meaning |
|---|---|
| `--config FILE` | Read configuration overrides from `FILE` instead of looking for `config.yaml` next to the executable, in the per-user folder (`~/Library/Application Support/CompressCompare/` on macOS, `$XDG_CONFIG_HOME/compresscompare/` or `~/.config/compresscompare/` on Linux) or in the working directory. The file must exist; the built-in defaults are always the base it merges into. |
| `--check-config` | Load the configuration exactly as a normal start would, print `<source>: OK (N sites)` or the error, and exit with status 0 or 1. No window is opened, so it works over SSH and in CI. `<source>` is `built-in config.yaml` when no override was found, otherwise the override's path. |
| `--system-info` | Load the configuration, initialise OpenCV exactly as a normal start would (thread pool, OpenCL device, kernel build and self-test), print the report below and exit with status 0. No window is opened. |
| `--exit-after SECONDS` | Close the window automatically `SECONDS` seconds after start-up (a positive decimal number). Meant for smoke tests: combined with files on the command line it exercises loading, comparison and rendering without a person present. |
| `--help`, `-h` | Print the usage text and exit. |
| `--version` | Print `CompressCompare 0.3.0` and exit. |

`--help` and `--version` do not read any configuration file; every other invocation does, and a broken
configuration stops the program before a window appears.

### `--system-info` output

```
CompressCompare 0.3.0
OpenCV:         4.12.0 (2 threads, SSE4.2 AVX AVX2 FMA3 AVX-512)
OpenCL device:  none
OpenCL in use:  no - no usable OpenCL device
Video back-end: FFMPEG (acceleration.video_backend: auto)
Still images:   OpenCV imgcodecs (JPEG, PNG, WebP, GIF, BMP, TIFF)
```

(from a Linux virtual machine without a GPU). The lines:

| Line | Content |
|---|---|
| `OpenCV` | OpenCV version, the number of threads its pool uses (`threading.max_workers`, capped by the CPU cores) and the SIMD instruction sets its dispatcher uses on this CPU |
| `OpenCL device` | the device OpenCV selected (name, OpenCL version, vendor), or `none` |
| `OpenCL in use` | `yes`, or `no - <reason>`: switched off in the configuration, no runtime or device, a CPU-only device in `auto` mode, a kernel build error, or a failed self-test with the share of differing values |
| `Video back-end` | the video back-ends compiled into OpenCV (`AVFOUNDATION` on macOS, `FFMPEG` on Linux, `none`) and the `acceleration.video_backend` setting. The `ffmpeg` executables are not part of this report |
| `Still images` | the still decoders: OpenCV's imgcodecs and, on macOS, ImageIO for HEIC/HEIF (and AVIF on macOS 13 and later) |

[How to use the GPU](../how-to/use-the-gpu.md) explains the OpenCL lines.

## Exit status

| Status | When |
|---|---|
| 0 | normal exit (window closed, `--check-config` passed, `--system-info`, `--help`, `--version`) |
| 1 | malformed command line, configuration error, `--check-config` failure, GLFW or OpenGL 3.3 initialisation failure |

Errors that stop the program are printed on stderr. An app started from Finder has no console, so on macOS
such a start simply ends; run the executable from Terminal to read the message.

## Console output

The program is a GUI application but writes a few lines to the console when started from one:

| Line | Meaning |
|---|---|
| `OpenCV <version>, OpenCL: <device>` or `OpenCV <version>, OpenCL: off (<reason>)` | printed at every start, after the configuration is loaded: whether heat maps, statistics and resampling will run through OpenCL |
| `configuration: <path>` | an override file was found and merged (nothing is printed for the built-in defaults) |
| `renderer: <message>` | the OpenGL renderer could not be initialised; the app runs but draws nothing |
| `GLFW error <code>: <text>` | passed through from GLFW |
| `N contract violation(s) were recorded during this session` | at exit, when a `CC_REQUIRE` / `CC_ENSURE` check failed during the run (each violation was also printed when it happened, with file and line). The program recovered from each one; report them, they are bugs. |

## Environment

| Variable | Use |
|---|---|
| `PATH` | searched for `ffmpeg` / `ffprobe` (after the executable's own directory and `ffmpeg.search_subdirs`, before `ffmpeg.extra_search_dirs`) and for `zenity` / `kdialog` on Linux |
| `HOME` (macOS) | `~/Library/Application Support/CompressCompare/` holds the override `config.yaml` and the UI layout file `ui.ini` (window positions, panel sizes). Delete `ui.ini` to reset the layout. |
| `XDG_CONFIG_HOME` or `HOME` (Linux) | `$XDG_CONFIG_HOME/compresscompare/` (`~/.config/compresscompare/` when `XDG_CONFIG_HOME` is not set) holds the per-user `config.yaml` and `ui.ini` |
| `OPENCV_OPENCL_DEVICE` | read by OpenCV to choose the OpenCL device; `acceleration.opencl_device` sets it when not empty. `:CPU:` selects a CPU device, `:GPU:0` the first GPU |

## Examples

```
compresscompare original.png instagram.jpg twitter.jpg
compresscompare original.mp4 "https://v.redd.it/abcdef/DASH_720.mp4"
compresscompare --config lab.yaml --check-config
compresscompare --system-info
compresscompare --exit-after 3 samples/original.png samples/instagram.jpg   # smoke test
```
