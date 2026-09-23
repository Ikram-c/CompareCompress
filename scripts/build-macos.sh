#!/usr/bin/env bash
# One-command build of CompressCompare for macOS 12 Monterey (Intel).
#
#   scripts/build-macos.sh            # dependencies, app, tests -> dist/CompressCompare.app
#   scripts/build-macos.sh --no-tests # skip the test run
#   scripts/build-macos.sh --clean    # remove build/ first (keeps the built OpenCV in deps/)
#
# Needs only the Xcode Command Line Tools (xcode-select --install) and an
# internet connection.  CMake is downloaded into deps/ when it is not
# installed.  The first run builds OpenCV (about 20-30 minutes on a dual-core
# MacBook Air); later runs reuse it and take a few minutes.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
RUN_TESTS=1
CLEAN=0
for arg in "$@"; do
  case "$arg" in
    --no-tests) RUN_TESTS=0 ;;
    --clean) CLEAN=1 ;;
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
    *) echo "unknown option $arg" >&2; exit 2 ;;
  esac
done

CMAKE_VERSION_WANTED="3.30.5"
DEPS="$ROOT/deps"
mkdir -p "$DEPS"

step() { printf '\n==> %s\n' "$*"; }

# ---- platform checks ---------------------------------------------------------
if [ "$(uname -s)" != "Darwin" ]; then
  echo "This script is for macOS. On Linux use: scripts/build-deps.sh && cmake --preset linux-system && cmake --build --preset linux-system" >&2
  exit 1
fi
OS_VERSION="$(sw_vers -productVersion)"
ARCH="$(uname -m)"
step "macOS $OS_VERSION on $ARCH"
case "$OS_VERSION" in
  10.*|11.*) echo "warning: this port targets macOS 12 or newer; the build may fail on $OS_VERSION" >&2 ;;
esac
if [ "$ARCH" != "x86_64" ]; then
  echo "note: building an x86_64 app on $ARCH (it runs under Rosetta 2)" >&2
fi

if ! xcode-select -p >/dev/null 2>&1; then
  echo "The Xcode Command Line Tools are missing. Run:  xcode-select --install  and then this script again." >&2
  exit 1
fi

# ---- CMake -------------------------------------------------------------------
cmake_ok() {
  command -v "$1" >/dev/null 2>&1 || return 1
  "$1" --version | head -1 | awk '{ split($3, v, "."); exit !((v[1] > 3) || (v[1] == 3 && v[2] >= 21)) }'
}
CMAKE=cmake
if ! cmake_ok cmake; then
  LOCAL_CMAKE="$DEPS/cmake/CMake.app/Contents/bin/cmake"
  if ! cmake_ok "$LOCAL_CMAKE"; then
    step "Downloading CMake $CMAKE_VERSION_WANTED (no CMake 3.21+ found)"
    tmp="$DEPS/cmake-download.tar.gz"
    curl -fL --retry 3 -o "$tmp" \
      "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION_WANTED}/cmake-${CMAKE_VERSION_WANTED}-macos10.10-universal.tar.gz"
    rm -rf "$DEPS/cmake" && mkdir -p "$DEPS/cmake"
    tar -xzf "$tmp" -C "$DEPS/cmake" --strip-components 1
    rm -f "$tmp"
  fi
  CMAKE="$LOCAL_CMAKE"
fi
export PATH="$(dirname "$CMAKE"):$PATH"
step "Using $("$CMAKE" --version | head -1)"

# ---- OpenCV ------------------------------------------------------------------
step "OpenCV (static, OpenCL + AVFoundation)"
"$ROOT/scripts/build-deps.sh"

# ---- the app -----------------------------------------------------------------
if [ "$CLEAN" = 1 ]; then
  rm -rf "$ROOT/build/macos"
fi
step "Configuring"
"$CMAKE" --preset macos-intel
step "Building"
"$CMAKE" --build --preset macos-intel

if [ "$RUN_TESTS" = 1 ]; then
  step "Running the tests (includes the GPU-vs-CPU check on this Mac's OpenCL driver)"
  ctest --test-dir "$ROOT/build/macos" --output-on-failure
fi

step "Packaging"
rm -rf "$ROOT/dist/CompressCompare.app"
"$CMAKE" --install "$ROOT/build/macos" --prefix "$ROOT/dist" >/dev/null
if command -v codesign >/dev/null 2>&1; then
  codesign --force --deep --sign - "$ROOT/dist/CompressCompare.app" >/dev/null 2>&1 || true
fi
"$ROOT/build/macos/CompressCompare.app/Contents/MacOS/CompressCompare" --system-info || true

cat <<EOF

Done: $ROOT/dist/CompressCompare.app
  open it:          open "$ROOT/dist/CompressCompare.app"
  try the samples:  open -a "$ROOT/dist/CompressCompare.app" --args "$ROOT/samples/original.png" "$ROOT/samples/instagram.jpg"
  benchmark:        "$ROOT/build/macos/cc_bench" "$ROOT/samples/original.png"
Drag it to /Applications to install. Your settings file goes in
  ~/Library/Application Support/CompressCompare/config.yaml
EOF
