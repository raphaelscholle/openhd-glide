#!/usr/bin/env bash
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BUILD_DIR="${GLIDE_BUILD_DIR:-${ROOT_DIR}/build-kms}"
BUILD_TYPE="${GLIDE_BUILD_TYPE:-Release}"
INSTALL_PREFIX="${GLIDE_INSTALL_PREFIX:-/usr/local}"
JOBS="${GLIDE_JOBS:-$(nproc)}"
INSTALL_DEPS="${GLIDE_INSTALL_DEPS:-0}"

if [ "${1:-}" = "--deps" ]; then
  INSTALL_DEPS=1
fi

if [ "$INSTALL_DEPS" = "1" ]; then
  sudo apt update
  sudo apt install -y \
    build-essential \
    cmake \
    pkg-config \
    libdrm-dev \
    libgbm-dev \
    libgles2-mesa-dev \
    libegl1-mesa-dev \
    libfreetype-dev \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-tools \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav
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
