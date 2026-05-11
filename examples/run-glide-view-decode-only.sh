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

exec sudo "$BIN" --udp-video --udp-port "$PORT"
