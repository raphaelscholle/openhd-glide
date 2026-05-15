#!/usr/bin/env bash
set -eu

DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
if [ -z "${GLIDE_BIN:-}" ]; then
  if [ -x "${DIR}/build-kms/openhd-glide" ]; then
    BIN="${DIR}/build-kms/openhd-glide"
  elif [ -x "${DIR}/../../../bin/openhd-glide" ]; then
    BIN="${DIR}/../../../bin/openhd-glide"
  else
    BIN="openhd-glide"
  fi
else
  BIN="$GLIDE_BIN"
fi
PORT="${1:-5600}"
WIDTH="${GLIDE_WIDTH:-1920}"
HEIGHT="${GLIDE_HEIGHT:-1080}"
FLOW_FPS="${GLIDE_FLOW_FPS:-30}"
DISPLAY_HZ="${GLIDE_DISPLAY_HZ:-120}"

exec sudo env \
  LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-/usr/lib/aarch64-linux-gnu}" \
  LIBGL_DRIVERS_PATH="${LIBGL_DRIVERS_PATH:-/usr/lib/aarch64-linux-gnu/dri}" \
  MESA_LOADER_DRIVER_OVERRIDE="${MESA_LOADER_DRIVER_OVERRIDE:-sun4i-drm}" \
  "$BIN" \
  --kms-video-preview \
  --gstreamer-video \
  --view-udp-port "$PORT" \
  --preview-width "$WIDTH" \
  --flow-height "$HEIGHT" \
  --flow-fps "$FLOW_FPS" \
  --display-refresh-hz "$DISPLAY_HZ"
