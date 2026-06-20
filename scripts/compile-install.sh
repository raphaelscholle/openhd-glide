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
# © OpenHD, All Rights Reserved.
################################################################################

set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BUILD_DIR="${GLIDE_BUILD_DIR:-${ROOT_DIR}/build-kms}"
BUILD_TYPE="${GLIDE_BUILD_TYPE:-Release}"
INSTALL_PREFIX="${GLIDE_INSTALL_PREFIX:-/usr/local}"
JOBS="${GLIDE_JOBS:-$(nproc)}"
INSTALL_DEPS="${GLIDE_INSTALL_DEPS:-0}"
DEPS_ONLY=0
APT_BACKPORTS_TARGET="${GLIDE_APT_BACKPORTS_TARGET:-bookworm-backports}"

if [ "${1:-}" = "--deps" ]; then
  INSTALL_DEPS=1
elif [ "${1:-}" = "--deps-only" ]; then
  INSTALL_DEPS=1
  DEPS_ONLY=1
fi

if [ "$INSTALL_DEPS" = "1" ]; then
  sudo apt update
  GRAPHICS_DEV_PACKAGES=(
    libdrm-dev
    libgbm-dev
    libgles2-mesa-dev
    libegl1-mesa-dev
  )
  GRAPHICS_RUNTIME_PACKAGES=(
    libdrm2
    libdrm-radeon1
    libdrm-nouveau2
    libdrm-amdgpu1
    libgbm1
    libegl-mesa0
    libglapi-mesa
  )

  if apt-cache policy libdrm2 libgbm1 | awk '/Installed:|Candidate:/ && /~bpo/ { found = 1 } END { exit(found ? 0 : 1) }'; then
    echo "Detected backports DRM/GBM packages; installing matching graphics development packages from ${APT_BACKPORTS_TARGET}"
    sudo apt install -y -t "$APT_BACKPORTS_TARGET" "${GRAPHICS_RUNTIME_PACKAGES[@]}"
    sudo apt install -y -t "$APT_BACKPORTS_TARGET" "${GRAPHICS_DEV_PACKAGES[@]}"
  else
    sudo apt install -y "${GRAPHICS_DEV_PACKAGES[@]}"
  fi

  sudo apt install -y \
    build-essential \
    cmake \
    pkg-config \
    libfreetype-dev \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-tools \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav

  if [ "$DEPS_ONLY" = "1" ]; then
    exit 0
  fi
fi

cmake \
  -S "$ROOT_DIR" \
  -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DOPENHD_GLIDE_DEVICE_KMS=ON \
  -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX"

cmake --build "$BUILD_DIR" -j"$JOBS"
sudo cmake --install "$BUILD_DIR"

echo "Installed OpenHD-Glide to ${INSTALL_PREFIX}/bin"
echo "Try: sudo ${INSTALL_PREFIX}/bin/openhd-glide --kms-video-preview --gstreamer-video --view-udp-port 5600 --preview-width 1920 --flow-height 1080"
