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

exec sudo "$BIN" \
  --kms-stack \
  --view-udp-port "$PORT" \
  --preview-width "$WIDTH" \
  --flow-height "$HEIGHT"
