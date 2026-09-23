# How to build on Linux

macOS is the target of this port; the Linux build exists for development and CI. It uses the same code,
the same pinned OpenCV configuration (with the FFmpeg libraries as its video back-end instead of
AVFoundation) and the same tests. The `linux-system` preset builds into `build/linux/`.

## 1. Install the toolchain and libraries (Debian, Ubuntu)

```
sudo apt install build-essential cmake git pkg-config \
     libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
     libglfw3-dev libgl1-mesa-dev libyaml-cpp-dev libcurl4-openssl-dev
```

- `cmake` must be 3.21 or newer.
- The FFmpeg development packages are for OpenCV's video back-end; without them OpenCV builds with no video
  reader, and videos then need the `ffmpeg` executable.
- GLFW and yaml-cpp are fetched from GitHub and built when no package is found; libcurl is optional (without
  it, URL fetching is disabled).
- Optional: `sudo apt install ffmpeg` for AVIF and HEIC stills (OpenCV's codecs do not read them on Linux)
  and for WebM, MKV and the other containers OpenCV cannot open; `zenity` or `kdialog` for file dialogs.

## 2. Build OpenCV, then the app

```
scripts/build-deps.sh                 # static OpenCV 4.12.0 into deps/opencv (about 12 min on 2 cores)
cmake --preset linux-system
cmake --build --preset linux-system
ctest --preset linux-system
```

Instead of `scripts/build-deps.sh` you can use any installed OpenCV 4.6 or newer with the `core`,
`imgproc`, `imgcodecs` and `videoio` modules (`sudo apt install libopencv-dev`, or `-DOpenCV_DIR=…`). A
distribution OpenCV is built with other codec versions, so decoded pixels, and therefore the numbers, can
differ slightly from the macOS build.

Run `build/linux/compresscompare`, and `build/linux/compresscompare --system-info` to see what was found.
The CMake options are the same as on macOS ([build on macOS](build-macos.md#cmake-options)).

## 3. Test the OpenCL path without a GPU

Most CI machines and virtual machines have no OpenCL device, so `test_opencl` prints *GPU checks skipped* and
passes. To run the GPU-against-CPU comparison anyway, install PoCL, an OpenCL implementation that runs on the
CPU:

```
sudo apt install pocl-opencl-icd ocl-icd-libopencl1 clinfo
clinfo -l                             # should list a "cpu" device
ctest --preset linux-system -R opencl
```

`test_opencl` forces OpenCL on, so it uses the PoCL device; the `opencl_cpu_device` test runs the same program
with `OPENCV_OPENCL_DEVICE=:CPU:`. The app itself does not use a CPU-only OpenCL device in the default
`auto` mode ([how to use the GPU](use-the-gpu.md)).

## Installing

`cmake --install build/linux --prefix /opt/compresscompare` copies the executable and `config.yaml` (the
editable copy the executable looks for next to itself) into `bin/`, and the docs and licences into the
prefix.
