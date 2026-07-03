#!/usr/bin/env bash
################################################################################
# OpenHD
#
# Licensed under the GNU General Public License (GPL) Version 3.
#
# This software is provided "as-is," without warranty of any kind, express or
# implied, including but not limited to the warranties of merchantability,
# fitness for a particular purpose, and non-infringement. For details, see the
# full license in the LICENSE file provided with this source code.
#
# Non-Military Use Only:
# This software and its associated components are explicitly intended for
# civilian and non-military purposes. Use in any military or defense
# applications is strictly prohibited unless explicitly and individually
# licensed otherwise by the OpenHD Team.
#
# Contributors:
# A full list of contributors can be found at the OpenHD GitHub repository:
# https://github.com/OpenHD
#
# (C) OpenHD, All Rights Reserved.
################################################################################

set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BUILD_DIR="${GLIDE_BUILD_DIR:-${ROOT_DIR}/build-kms}"
BUILD_TYPE="${GLIDE_BUILD_TYPE:-Release}"
INSTALL_PREFIX="${GLIDE_INSTALL_PREFIX:-/usr/local}"
JOBS="${GLIDE_JOBS:-$(nproc 2>/dev/null || printf '1')}"
INSTALL="${GLIDE_INSTALL:-0}"
INSTALL_DEPS="${GLIDE_INSTALL_DEPS:-ask}"
ALLOW_MISSING_DEPS="${GLIDE_ALLOW_MISSING_DEPS:-0}"
PLATFORM_OVERRIDE="${GLIDE_PLATFORM:-auto}"
APT_BACKPORTS_TARGET="${GLIDE_APT_BACKPORTS_TARGET:-bookworm-backports}"

usage() {
  cat <<EOF
Usage: scripts/build-auto.sh [options]

Options:
  --deps              install missing packages, then build
  --deps-only         install missing packages and exit
  --install           install OpenHD-Glide after building
  --allow-missing     configure even when optional video dependencies are missing
  --platform NAME     override platform detection: raspberrypi, rockchip, allwinner, generic
  --build-dir DIR     override build directory
  --prefix DIR        override install prefix
  -j N                build parallelism
  -h, --help          show this help

Environment overrides:
  GLIDE_INSTALL_DEPS=0|1|ask
  GLIDE_ALLOW_MISSING_DEPS=0|1
  GLIDE_REQUIRE_RKMPP=0|1
EOF
}

DEPS_ONLY=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --deps)
      INSTALL_DEPS=1
      ;;
    --deps-only)
      INSTALL_DEPS=1
      DEPS_ONLY=1
      ;;
    --install)
      INSTALL=1
      ;;
    --allow-missing)
      ALLOW_MISSING_DEPS=1
      ;;
    --platform)
      shift
      PLATFORM_OVERRIDE="${1:-}"
      ;;
    --build-dir)
      shift
      BUILD_DIR="${1:-}"
      ;;
    --prefix)
      shift
      INSTALL_PREFIX="${1:-}"
      ;;
    -j)
      shift
      JOBS="${1:-}"
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
  shift
done

enabled() {
  case "$(printf '%s' "${1:-}" | tr '[:upper:]' '[:lower:]')" in
    1|true|yes|on|enabled)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

have_command() {
  command -v "$1" >/dev/null 2>&1
}

compatible_text() {
  if [ -r /proc/device-tree/compatible ]; then
    tr '\0' '\n' </proc/device-tree/compatible 2>/dev/null || true
  fi
}

cpuinfo_text() {
  if [ -r /proc/cpuinfo ]; then
    cat /proc/cpuinfo
  fi
}

detect_platform() {
  if [ "$PLATFORM_OVERRIDE" != "auto" ]; then
    printf '%s\n' "$PLATFORM_OVERRIDE" | tr '[:upper:]' '[:lower:]'
    return 0
  fi

  text="$(compatible_text; cpuinfo_text)"
  lower="$(printf '%s' "$text" | tr '[:upper:]' '[:lower:]')"
  case "$lower" in
    *raspberry*|*brcm,bcm27*|*bcm2711*|*bcm2712*)
      printf 'raspberrypi\n'
      ;;
    *rockchip*|*rk3566*|*rk3568*|*rk3588*|*rk3576*)
      printf 'rockchip\n'
      ;;
    *allwinner*|*sunxi*|*sun50i*|*sun8i*|*sun4i*)
      printf 'allwinner\n'
      ;;
    *)
      printf 'generic\n'
      ;;
  esac
}

apt_available() {
  have_command apt-get && have_command apt-cache
}

pkg_config_has() {
  have_command pkg-config && pkg-config --exists "$1" >/dev/null 2>&1
}

gst_element_has() {
  have_command gst-inspect-1.0 && gst-inspect-1.0 "$1" >/dev/null 2>&1
}

missing_pkg_modules() {
  for module in "$@"; do
    if ! pkg_config_has "$module"; then
      printf '%s\n' "$module"
    fi
  done
}

missing_commands() {
  for command_name in "$@"; do
    if ! have_command "$command_name"; then
      printf '%s\n' "$command_name"
    fi
  done
}

install_apt_packages() {
  if [ "$#" -eq 0 ]; then
    return 0
  fi
  if ! apt_available; then
    echo "Automatic dependency installation currently supports apt-based systems only." >&2
    echo "Install the listed packages manually, or run with --allow-missing for a degraded build." >&2
    return 1
  fi

  sudo apt-get update

  graphics_dev_packages="libdrm-dev libgbm-dev libgles2-mesa-dev libegl1-mesa-dev"
  graphics_runtime_packages="libdrm2 libdrm-radeon1 libdrm-nouveau2 libdrm-amdgpu1 libgbm1 libegl-mesa0 libglapi-mesa"
  if apt-cache policy libdrm2 libgbm1 | awk '/Installed:|Candidate:/ && /~bpo/ { found = 1 } END { exit(found ? 0 : 1) }'; then
    echo "Detected backports DRM/GBM packages; installing matching graphics packages from ${APT_BACKPORTS_TARGET}."
    sudo apt-get install -y -t "$APT_BACKPORTS_TARGET" $graphics_runtime_packages
    sudo apt-get install -y -t "$APT_BACKPORTS_TARGET" $graphics_dev_packages
  fi

  sudo apt-get install -y "$@"
}

confirm_install() {
  if [ "$INSTALL_DEPS" = "1" ]; then
    return 0
  fi
  if [ "$INSTALL_DEPS" = "0" ]; then
    return 1
  fi
  if [ ! -t 0 ]; then
    return 1
  fi
  printf 'Install missing packages with apt now? [y/N] '
  read -r answer
  case "$(printf '%s' "$answer" | tr '[:upper:]' '[:lower:]')" in
    y|yes)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

PLATFORM="$(detect_platform)"
echo "Detected platform: ${PLATFORM}"

BASE_PACKAGES="
build-essential
cmake
pkg-config
libdrm-dev
libgbm-dev
libgles2-mesa-dev
libegl1-mesa-dev
libfreetype-dev
libgstreamer1.0-dev
libgstreamer-plugins-base1.0-dev
gstreamer1.0-tools
gstreamer1.0-plugins-good
gstreamer1.0-plugins-bad
gstreamer1.0-plugins-ugly
gstreamer1.0-libav
"

PLATFORM_PACKAGES=""
REQUIRE_RKMPP="${GLIDE_REQUIRE_RKMPP:-0}"
case "$PLATFORM" in
  rockchip)
    PLATFORM_PACKAGES="librockchip-mpp-dev librga-dev gstreamer1.0-rockchip1"
    REQUIRE_RKMPP="${GLIDE_REQUIRE_RKMPP:-1}"
    ;;
  raspberrypi)
    PLATFORM_PACKAGES=""
    ;;
  allwinner)
    PLATFORM_PACKAGES=""
    ;;
esac

if [ "$INSTALL_DEPS" = "1" ]; then
  install_apt_packages $BASE_PACKAGES $PLATFORM_PACKAGES
  if [ "$DEPS_ONLY" = "1" ]; then
    exit 0
  fi
fi

REQUIRED_COMMANDS="cmake pkg-config"
REQUIRED_MODULES="libdrm gbm egl glesv2 freetype2 gstreamer-1.0 gstreamer-app-1.0 gstreamer-allocators-1.0 gstreamer-video-1.0"
MISSING_COMMANDS="$(missing_commands $REQUIRED_COMMANDS)"
MISSING_MODULES="$(missing_pkg_modules $REQUIRED_MODULES)"
MISSING_GST_ELEMENTS=""
if ! gst_element_has rtph264depay; then
  MISSING_GST_ELEMENTS="${MISSING_GST_ELEMENTS} rtph264depay"
fi
if ! gst_element_has h264parse; then
  MISSING_GST_ELEMENTS="${MISSING_GST_ELEMENTS} h264parse"
fi
if [ "$PLATFORM" = "raspberrypi" ] && ! gst_element_has v4l2h264dec; then
  MISSING_GST_ELEMENTS="${MISSING_GST_ELEMENTS} v4l2h264dec"
fi
if [ "$PLATFORM" = "rockchip" ] && enabled "$REQUIRE_RKMPP" && [ ! -e /usr/lib/aarch64-linux-gnu/librockchip_mpp.so ] && [ ! -e /usr/lib/arm-linux-gnueabihf/librockchip_mpp.so ] && [ ! -e /usr/lib/librockchip_mpp.so ]; then
  MISSING_MODULES="${MISSING_MODULES}
rockchip_mpp library"
fi

if [ -n "$MISSING_COMMANDS" ] || [ -n "$MISSING_MODULES" ] || [ -n "$MISSING_GST_ELEMENTS" ]; then
  echo "Missing build/video dependencies were detected:"
  if [ -n "$MISSING_COMMANDS" ]; then
    printf '  commands:\n%s\n' "$MISSING_COMMANDS" | sed 's/^/    /'
  fi
  if [ -n "$MISSING_MODULES" ]; then
    printf '  pkg-config/libraries:\n%s\n' "$MISSING_MODULES" | sed 's/^/    /'
  fi
  if [ -n "$MISSING_GST_ELEMENTS" ]; then
    printf '  GStreamer elements:%s\n' "$MISSING_GST_ELEMENTS"
  fi

  if apt_available && confirm_install; then
    install_apt_packages $BASE_PACKAGES $PLATFORM_PACKAGES
  elif ! enabled "$ALLOW_MISSING_DEPS"; then
    echo "Install dependencies with: scripts/build-auto.sh --deps" >&2
    echo "Or continue with a degraded configure using: scripts/build-auto.sh --allow-missing" >&2
    exit 1
  else
    echo "Continuing despite missing dependencies because --allow-missing was set."
  fi
fi

CMAKE_ARGS="
-DCMAKE_BUILD_TYPE=${BUILD_TYPE}
-DOPENHD_GLIDE_DEVICE_KMS=ON
-DOPENHD_GLIDE_REQUIRE_KMS_GBM=ON
-DOPENHD_GLIDE_REQUIRE_GSTREAMER=ON
-DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX}
"

if enabled "$ALLOW_MISSING_DEPS"; then
  CMAKE_ARGS="
${CMAKE_ARGS}
-DOPENHD_GLIDE_REQUIRE_KMS_GBM=OFF
-DOPENHD_GLIDE_REQUIRE_GSTREAMER=OFF
"
fi

if enabled "$REQUIRE_RKMPP"; then
  CMAKE_ARGS="
${CMAKE_ARGS}
-DOPENHD_GLIDE_REQUIRE_RKMPP=ON
"
else
  CMAKE_ARGS="
${CMAKE_ARGS}
-DOPENHD_GLIDE_REQUIRE_RKMPP=OFF
"
fi

case "$PLATFORM" in
  raspberrypi)
    echo "Configuring Raspberry Pi KMS video build. Expected runtime driver: vc4-kms-v3d."
    ;;
  rockchip)
    echo "Configuring Rockchip KMS build. Native RKMPP requirement: ${REQUIRE_RKMPP}."
    ;;
  allwinner)
    echo "Configuring Allwinner KMS build. Cedar BSP libraries are detected by CMake when installed."
    ;;
  *)
    echo "Configuring generic Linux KMS build."
    ;;
esac

# shellcheck disable=SC2086
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" $CMAKE_ARGS
cmake --build "$BUILD_DIR" -j"$JOBS"

if enabled "$INSTALL"; then
  sudo cmake --install "$BUILD_DIR"
  echo "Installed OpenHD-Glide to ${INSTALL_PREFIX}/bin"
fi

case "$PLATFORM" in
  raspberrypi)
    echo "Try: sudo ${BUILD_DIR}/openhd-glide --kms-video-preview --gstreamer-video --no-flow --view-udp-port 5600 --preview-width 0 --flow-height 0"
    echo "Or:  examples/run-kms-video-rpi-video-only.sh 5600 h264"
    ;;
  rockchip)
    echo "Try: examples/run-kms-video-rkmpp-video-only.sh 5600 h264"
    ;;
  allwinner)
    echo "Try: examples/run-kms-video-cedar-video-only.sh 5600"
    ;;
  *)
    echo "Try: examples/run-kms-video-gstreamer-video-only.sh 5600 h264"
    ;;
esac
