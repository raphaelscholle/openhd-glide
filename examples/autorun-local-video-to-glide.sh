#!/usr/bin/env bash
################################################################################
# OpenHD
#
# Licensed under the GNU General Public License (GPL) Version 3.
################################################################################

set -eu

CONFIG_FILE="${OPENHD_GLIDE_STREAM_CONFIG:-/etc/default/openhd-glide-stream}"
if [ -r "$CONFIG_FILE" ]; then
  # shellcheck disable=SC1090
  . "$CONFIG_FILE"
fi

TARGET="${GLIDE_STREAM_TARGET:-127.0.0.1}"
PORT="${GLIDE_STREAM_PORT:-${GLIDE_VIEW_PORT:-5600}}"
CODEC="${GLIDE_STREAM_CODEC:-${GLIDE_VIEW_CODEC:-h264}}"
VIDEO_FILE="${GLIDE_STREAM_FILE:-/home/openhd/battlefield_1080p_120fps_8mbps.mp4}"
MODE="${GLIDE_STREAM_MODE:-realtime}"

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
SENDER="${SCRIPT_DIR}/stream-blurbusters-1080p120-to-glide-view.sh"

if [ ! -x "$SENDER" ]; then
  echo "openhd-glide-stream: missing sender script: $SENDER" >&2
  exit 1
fi

if [ ! -s "$VIDEO_FILE" ]; then
  echo "openhd-glide-stream: missing video file: $VIDEO_FILE" >&2
  exit 1
fi

ARGS=("$TARGET" "$PORT" "$CODEC" "$VIDEO_FILE")
case "$MODE" in
  fast|--fast)
    ARGS+=(--fast)
    ;;
  realtime|"")
    ;;
  *)
    echo "openhd-glide-stream: unsupported GLIDE_STREAM_MODE '$MODE'; use realtime or fast" >&2
    exit 2
    ;;
esac

exec "$SENDER" "${ARGS[@]}"
