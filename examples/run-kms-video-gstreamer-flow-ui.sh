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
if [ -z "${GLIDE_UI_BIN:-}" ]; then
  if [ -x "${DIR}/build-kms/glide-ui" ]; then
    UI_BIN="${DIR}/build-kms/glide-ui"
  elif [ -x "${DIR}/glide-ui" ]; then
    UI_BIN="${DIR}/glide-ui"
  elif [ -x "${DIR}/../../../bin/glide-ui" ]; then
    UI_BIN="${DIR}/../../../bin/glide-ui"
  else
    UI_BIN="glide-ui"
  fi
else
  UI_BIN="$GLIDE_UI_BIN"
fi

PORT="${1:-5600}"
CODEC="${2:-${GLIDE_VIEW_CODEC:-${GLIDE_CODEC:-h264}}}"
WIDTH="${GLIDE_WIDTH:-1920}"
HEIGHT="${GLIDE_HEIGHT:-1080}"
FLOW_FPS="${GLIDE_FLOW_FPS:-120}"
DISPLAY_HZ="${GLIDE_DISPLAY_HZ:-120}"
UI_WIDTH="${GLIDE_UI_WIDTH:-760}"
UI_HEIGHT="${GLIDE_UI_HEIGHT:-$HEIGHT}"
UI_COLOR="${GLIDE_UI_COLOR:-0xCC0B1722}"
UI_BUFFER="${GLIDE_UI_BUFFER:-/tmp/openhd-glide-ui.argb}"
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

rm -f "$UI_BUFFER"
"$UI_BIN" \
  --buffer \
  --width "$UI_WIDTH" \
  --height "$UI_HEIGHT" \
  --buffer-path "$UI_BUFFER" &
UI_PID=$!
cleanup() {
  kill "$UI_PID" 2>/dev/null || true
  wait "$UI_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

SUDO_ARGS=()
if ! [ -t 0 ]; then
  SUDO_ARGS=(-S)
fi

ENV_ARGS=(
  "LD_LIBRARY_PATH=${GLIDE_LD_LIBRARY_PATH:-/usr/lib/aarch64-linux-gnu}"
  "LIBGL_DRIVERS_PATH=${GLIDE_LIBGL_DRIVERS_PATH:-/usr/lib/aarch64-linux-gnu/dri}"
)
if [ -n "${GLIDE_MESA_DRIVER_OVERRIDE:-}" ]; then
  ENV_ARGS+=("MESA_LOADER_DRIVER_OVERRIDE=$GLIDE_MESA_DRIVER_OVERRIDE")
fi

sudo "${SUDO_ARGS[@]}" env "${ENV_ARGS[@]}" \
  "$BIN" \
  --kms-video-preview \
  --gstreamer-video \
  --view-udp-port "$PORT" \
  --view-udp-codec "$CODEC" \
  --preview-width "$WIDTH" \
  --flow-height "$HEIGHT" \
  --flow-fps "$FLOW_FPS" \
  --display-refresh-hz "$DISPLAY_HZ" \
  --ui-overlay \
  --ui-width "$UI_WIDTH" \
  --ui-height "$UI_HEIGHT" \
  --ui-buffer-path "$UI_BUFFER" \
  --ui-debug-color "$UI_COLOR"
