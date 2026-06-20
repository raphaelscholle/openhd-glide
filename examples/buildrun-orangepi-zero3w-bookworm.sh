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
# (c) OpenHD, All Rights Reserved.
################################################################################

set -eu

DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PORT="${1:-${GLIDE_VIEW_PORT:-5600}}"
CODEC="${2:-${GLIDE_VIEW_CODEC:-h264}}"
MODE="${GLIDE_ORANGEPI_MODE:-all}"
BACKEND="${GLIDE_ORANGEPI_BACKEND:-cedar}"
BUILD_DIR="${GLIDE_BUILD_DIR:-${DIR}/build-orangepi-zero3w-bookworm}"
INSTALL_PREFIX="${GLIDE_INSTALL_PREFIX:-/usr/local}"

usage() {
  cat <<EOF
Usage: $0 [udp-port] [h264|h265]

Orange Pi Zero 3W Bookworm build/run helper for OpenHD-Glide.

Environment:
  GLIDE_ORANGEPI_MODE=all|deps|build|run      Default: all
  GLIDE_ORANGEPI_BACKEND=cedar|gstreamer      Default: cedar
  GLIDE_BUILD_DIR=<path>                      Default: ${DIR}/build-orangepi-zero3w-bookworm
  GLIDE_INSTALL_PREFIX=<path>                 Default: /usr/local
  GLIDE_WIDTH=<pixels|auto>                   Default: auto
  GLIDE_HEIGHT=<pixels|auto>                  Default: auto
  GLIDE_DISPLAY_HZ=<hz|0>                     Default: 0
  GLIDE_FLOW_FPS=<fps>                        Default: 30 for cedar helper

Examples:
  $0
  GLIDE_ORANGEPI_MODE=deps $0
  GLIDE_ORANGEPI_MODE=run GLIDE_ORANGEPI_BACKEND=gstreamer $0 5600 h264
EOF
}

case "${1:-}" in
  -h|--help)
    usage
    exit 0
    ;;
esac

run_deps() {
  GLIDE_BUILD_DIR="$BUILD_DIR" \
  GLIDE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    "${DIR}/scripts/compile-install.sh" --deps-only
}

run_build() {
  GLIDE_BUILD_DIR="$BUILD_DIR" \
  GLIDE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    "${DIR}/scripts/compile-install.sh"
}

run_stack() {
  export GLIDE_BIN="${GLIDE_BIN:-${BUILD_DIR}/openhd-glide}"
  export GLIDE_WIDTH="${GLIDE_WIDTH:-auto}"
  export GLIDE_HEIGHT="${GLIDE_HEIGHT:-auto}"
  export GLIDE_DISPLAY_HZ="${GLIDE_DISPLAY_HZ:-0}"
  export GLIDE_LD_LIBRARY_PATH="${GLIDE_LD_LIBRARY_PATH:-/usr/lib/aarch64-linux-gnu}"
  export GLIDE_LIBGL_DRIVERS_PATH="${GLIDE_LIBGL_DRIVERS_PATH:-/usr/lib/aarch64-linux-gnu/dri}"
  export GLIDE_MESA_DRIVER_OVERRIDE="${GLIDE_MESA_DRIVER_OVERRIDE:-sun4i-drm}"

  case "$BACKEND" in
    cedar|allwinner|native-cedar)
      "${DIR}/examples/run-kms-video-cedar-flow.sh" "$PORT"
      ;;
    gstreamer|gst)
      "${DIR}/examples/run-kms-video-gstreamer-flow-ui.sh" "$PORT" "$CODEC"
      ;;
    *)
      echo "unsupported GLIDE_ORANGEPI_BACKEND '${BACKEND}'; use cedar or gstreamer" >&2
      exit 2
      ;;
  esac
}

case "$MODE" in
  all)
    run_deps
    run_build
    run_stack
    ;;
  deps)
    run_deps
    ;;
  build)
    run_build
    ;;
  run)
    run_stack
    ;;
  *)
    echo "unsupported GLIDE_ORANGEPI_MODE '${MODE}'; use all, deps, build, or run" >&2
    exit 2
    ;;
esac
