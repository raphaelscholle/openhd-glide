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
CODEC="${2:-${GLIDE_VIEW_CODEC:-${GLIDE_CODEC:-h264}}}"
WIDTH="${GLIDE_WIDTH:-1920}"
HEIGHT="${GLIDE_HEIGHT:-1080}"
FLOW_FPS="${GLIDE_FLOW_FPS:-30}"
DISPLAY_HZ="${GLIDE_DISPLAY_HZ:-120}"
CODEC="$(printf "%s" "$CODEC" | tr '[:upper:]' '[:lower:]')"
case "$CODEC" in
  h264|avc)
    CODEC="h264"
    ;;
  h265|hevc)
    CODEC="h265"
    ;;
  *)
    echo "unsupported codec '${CODEC}'; use h264 or h265" >&2
    exit 2
    ;;
esac

exec sudo env \
  LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-/usr/lib/aarch64-linux-gnu}" \
  LIBGL_DRIVERS_PATH="${LIBGL_DRIVERS_PATH:-/usr/lib/aarch64-linux-gnu/dri}" \
  MESA_LOADER_DRIVER_OVERRIDE="${MESA_LOADER_DRIVER_OVERRIDE:-sun4i-drm}" \
  "$BIN" \
  --kms-video-preview \
  --gstreamer-video \
  --view-udp-port "$PORT" \
  --view-udp-codec "$CODEC" \
  --preview-width "$WIDTH" \
  --flow-height "$HEIGHT" \
  --flow-fps "$FLOW_FPS" \
  --display-refresh-hz "$DISPLAY_HZ"
