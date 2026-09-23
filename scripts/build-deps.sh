#!/usr/bin/env bash
# Builds the pinned OpenCV release as static libraries into deps/opencv.
#
# Only the four modules the app uses are compiled (core, imgproc, imgcodecs,
# videoio), with the image codecs taken from OpenCV's bundled sources, so
# nothing has to come from Homebrew or MacPorts.  On macOS the result targets
# macOS 12.0 / x86_64 and uses Apple's OpenCL framework plus AVFoundation for
# video; on Linux (used for CI and development) it uses the system FFmpeg.
#
# Usage:  scripts/build-deps.sh            # build (skips work that is done)
#         JOBS=2 scripts/build-deps.sh     # limit parallel compile jobs
#         WITH_IPP=OFF scripts/build-deps.sh
# Takes about 20-30 minutes on a dual-core 2015-2017 MacBook Air.
set -euo pipefail

OPENCV_VERSION="${OPENCV_VERSION:-4.12.0}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS="${CC_DEPS_DIR:-$ROOT/deps}"
SRC="$DEPS/src/opencv-$OPENCV_VERSION"
BUILD="$DEPS/build/opencv-$OPENCV_VERSION"
PREFIX="$DEPS/opencv"
WITH_IPP="${WITH_IPP:-ON}"

if command -v sysctl >/dev/null 2>&1 && sysctl -n hw.ncpu >/dev/null 2>&1; then
  JOBS="${JOBS:-$(sysctl -n hw.ncpu)}"
else
  JOBS="${JOBS:-$(nproc)}"
fi

need() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "error: '$1' not found. $2" >&2
    exit 1
  fi
}
need git "Install the Xcode Command Line Tools: xcode-select --install"
need cmake "Install CMake 3.21 or newer (https://cmake.org/download/ has a macOS 10.13+ universal .dmg)"

if [ -f "$PREFIX/.built-$OPENCV_VERSION" ]; then
  echo "OpenCV $OPENCV_VERSION already built in $PREFIX"
  exit 0
fi

mkdir -p "$DEPS/src" "$DEPS/build"
if [ ! -d "$SRC/.git" ] && [ ! -f "$SRC/CMakeLists.txt" ]; then
  echo "Fetching OpenCV $OPENCV_VERSION ..."
  git clone --depth 1 --branch "$OPENCV_VERSION" https://github.com/opencv/opencv.git "$SRC"
fi

GENERATOR=()
if command -v ninja >/dev/null 2>&1; then
  GENERATOR=(-G Ninja)
fi

# libjpeg-turbo's SIMD code (2-3x faster JPEG decoding) needs the NASM
# assembler, which the Xcode tools do not include.  Build it locally when it is
# missing; without it OpenCV still builds, just with the plain-C JPEG path.
NASM_VERSION="2.16.03"
if [ "$(uname -s)" = "Darwin" ] && ! command -v nasm >/dev/null 2>&1 && [ "${WITH_NASM:-ON}" = "ON" ]; then
  if [ ! -x "$DEPS/tools/bin/nasm" ]; then
    echo "Building NASM $NASM_VERSION for libjpeg-turbo's SIMD code ..."
    (
      set -e
      mkdir -p "$DEPS/src" && cd "$DEPS/src"
      curl -fL --retry 3 -o nasm.tar.xz "https://www.nasm.us/pub/nasm/releasebuilds/${NASM_VERSION}/nasm-${NASM_VERSION}.tar.xz"
      tar -xJf nasm.tar.xz && rm -f nasm.tar.xz
      cd "nasm-${NASM_VERSION}"
      ./configure --prefix="$DEPS/tools" >/dev/null
      make -j "$JOBS" >/dev/null
      make install >/dev/null
    ) || echo "warning: NASM could not be built; JPEG decoding will use the slower C path (or: sudo port install nasm)" >&2
  fi
  if [ -x "$DEPS/tools/bin/nasm" ]; then
    export PATH="$DEPS/tools/bin:$PATH"
  fi
fi

COMMON=(
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_INSTALL_PREFIX="$PREFIX"
  -DBUILD_SHARED_LIBS=OFF
  -DBUILD_LIST=core,imgproc,imgcodecs,videoio
  -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOCS=OFF -DBUILD_opencv_apps=OFF
  -DBUILD_JAVA=OFF -DBUILD_opencv_python3=OFF -DBUILD_opencv_python_bindings_generator=OFF
  -DBUILD_opencv_js_bindings_generator=OFF -DBUILD_opencv_objc_bindings_generator=OFF
  -DOPENCV_GENERATE_PKGCONFIG=OFF
  # image codecs from OpenCV's own 3rdparty tree (static, no system packages)
  -DBUILD_ZLIB=ON -DBUILD_JPEG=ON -DBUILD_PNG=ON -DBUILD_WEBP=ON -DBUILD_TIFF=ON
  -DWITH_JPEG=ON -DWITH_PNG=ON -DWITH_WEBP=ON -DWITH_TIFF=ON -DWITH_IMGCODEC_GIF=ON
  -DWITH_OPENJPEG=OFF -DWITH_JASPER=OFF -DWITH_OPENEXR=OFF -DWITH_AVIF=OFF -DWITH_SPNG=OFF
  -DWITH_IMGCODEC_HDR=OFF -DWITH_IMGCODEC_SUNRASTER=OFF -DWITH_IMGCODEC_PXM=OFF -DWITH_IMGCODEC_PFM=OFF
  # acceleration
  -DWITH_OPENCL=ON -DWITH_IPP="$WITH_IPP" -DWITH_EIGEN=OFF -DWITH_LAPACK=OFF -DWITH_TBB=OFF -DWITH_OPENMP=OFF
  # everything else off
  -DWITH_PROTOBUF=OFF -DWITH_ADE=OFF -DWITH_QT=OFF -DWITH_GTK=OFF -DWITH_VTK=OFF -DWITH_ITT=OFF
  -DWITH_1394=OFF -DWITH_GSTREAMER=OFF -DWITH_V4L=OFF -DWITH_OBSENSOR=OFF -DWITH_QUIRC=OFF
  -DWITH_OPENVX=OFF -DWITH_VA=OFF -DWITH_VA_INTEL=OFF -DWITH_CUDA=OFF -DWITH_FASTCV=OFF -DWITH_KLEIDICV=OFF
)

case "$(uname -s)" in
  Darwin)
    PLATFORM=(
      -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
      -DCMAKE_OSX_ARCHITECTURES=x86_64
      -DWITH_AVFOUNDATION=ON
      -DWITH_FFMPEG=OFF
    )
    ;;
  *)
    PLATFORM=(-DWITH_FFMPEG=ON)
    ;;
esac

echo "Configuring OpenCV $OPENCV_VERSION (static, OpenCL on) ..."
cmake -S "$SRC" -B "$BUILD" "${GENERATOR[@]}" "${COMMON[@]}" "${PLATFORM[@]}"
echo "Building with $JOBS jobs ..."
cmake --build "$BUILD" --parallel "$JOBS"
cmake --install "$BUILD"
touch "$PREFIX/.built-$OPENCV_VERSION"
echo "OpenCV $OPENCV_VERSION installed in $PREFIX"
