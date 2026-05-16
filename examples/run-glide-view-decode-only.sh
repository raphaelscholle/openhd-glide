#!/usr/bin/env bash
set -eu

DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
if [ -z "${GLIDE_VIEW_BIN:-}" ]; then
  if [ -x "${DIR}/build-kms/glide-view" ]; then
    BIN="${DIR}/build-kms/glide-view"
  elif [ -x "${DIR}/../../../bin/glide-view" ]; then
    BIN="${DIR}/../../../bin/glide-view"
  else
    BIN="glide-view"
  fi
else
  BIN="$GLIDE_VIEW_BIN"
fi
PORT="${1:-5600}"
CODEC="${2:-${GLIDE_VIEW_CODEC:-${GLIDE_CODEC:-h264}}}"
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

exec sudo "$BIN" --udp-video --udp-port "$PORT" --udp-codec "$CODEC"
