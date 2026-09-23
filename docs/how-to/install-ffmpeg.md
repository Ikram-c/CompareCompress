# How to install ffmpeg (optional on macOS)

The `ffprobe` and `ffmpeg` programs are not part of CompressCompare. When they are installed, the app runs
them as a fallback: to open videos its own video back-end cannot read, and to decode still formats the
built-in decoders cannot read. Whether you need them depends on the platform.

## Do you need it?

**On macOS, usually not.** Videos open through OpenCV's AVFoundation back-end, which uses the system's
(hardware) H.264 and HEVC decoders, and HEIC/HEIF photos are decoded by macOS ImageIO. Install ffmpeg only if
you want to open:

- videos in containers AVFoundation does not read: **WebM, MKV, FLV** and similar (MP4, MOV and M4V work
  without ffmpeg);
- **AVIF** stills on macOS 12 (ImageIO reads AVIF from macOS 13 on);
- anything else the built-in decoders reject, which the app then hands to ffmpeg as a last attempt;

or if you set `acceleration.video_backend: ffmpeg` to decode every video with the executable.

**On Linux, for AVIF and HEIC.** OpenCV's codecs do not read them there, so those stills need the ffmpeg
executable. Videos open through OpenCV's FFmpeg back-end when OpenCV was built with it
([build on Linux](build-linux.md)); the executable is also the fallback for files that back-end cannot open.

JPEG, PNG, WebP, GIF, BMP and TIFF never need ffmpeg.

## Where the app looks

In this order, and it stops at the first hit:

1. next to the executable – inside the app bundle that is `CompressCompare.app/Contents/MacOS/`;
2. the sub-folders listed in `ffmpeg.search_subdirs` of `config.yaml`, relative to the executable
   (`ffmpeg`, `ffmpeg/bin`, `bin`, `tools`, `../ffmpeg/bin` by default);
3. every directory on `PATH`;
4. the directories in `ffmpeg.extra_search_dirs`: `/usr/local/bin`, `/opt/local/bin` (MacPorts) and
   `/opt/homebrew/bin` by default.

The last step matters on macOS: an app started from Finder or the Dock gets a minimal `PATH`, so a copy
installed in one of those three folders is still found.

The *Settings* window (`Cmd+P` on macOS, `Ctrl+P` on Linux) shows the version it found, or *not found
(optional)* with a **re-detect ffmpeg** button, so you can install ffmpeg while the app is running.

## macOS 12

Homebrew no longer supports macOS 12, so use one of these:

- **MacPorts** (<https://www.macports.org/install.php> has an installer for Monterey):
  ```
  sudo port install ffmpeg
  ```
  The programs land in `/opt/local/bin`, which is on the app's search list. MacPorts may build ffmpeg and its
  dependencies from source, which takes a while on a dual-core Mac.
- **A static build**: download `ffmpeg` and `ffprobe` for Intel Macs (for example from
  <https://evermeet.cx/ffmpeg/>) and copy both into `/usr/local/bin`:
  ```
  sudo mkdir -p /usr/local/bin
  sudo cp ffmpeg ffprobe /usr/local/bin/
  sudo xattr -d com.apple.quarantine /usr/local/bin/ffmpeg /usr/local/bin/ffprobe
  ```
  The last line removes the download quarantine flag, without which macOS refuses to run the programs (it
  reports an error if a file has no flag, which is harmless). Copying them into
  `CompressCompare.app/Contents/MacOS/` works too, but the next install of the app replaces the bundle.

Both programs are needed; the app treats ffmpeg as missing when either is absent.

## Linux

`sudo apt install ffmpeg` (Debian/Ubuntu), `sudo dnf install ffmpeg` (Fedora, needs RPM Fusion), or
`sudo pacman -S ffmpeg` (Arch). Both programs land on `PATH`.

## Checking

Open *Settings*: the line *ffmpeg: ffmpeg version …* confirms it. Load a WebM file into a slot: its thumbnail
tooltip ends with *decoded with ffmpeg*. (`--system-info` reports the video back-end compiled into OpenCV,
which does not depend on the ffmpeg programs.)

## Time-outs and caps

Every ffmpeg / ffprobe run has a time-out and an output cap so a broken file cannot hang the app:
`process.probe_timeout_ms` (30 s) for ffprobe, `process.default_timeout_ms` (60 s) for frame extraction, and
the `limits.video.*` caps refuse files that are too large before they are opened, and videos that are too
long or too big right after probing. All of them are in [config.yaml](../reference/config.md).
