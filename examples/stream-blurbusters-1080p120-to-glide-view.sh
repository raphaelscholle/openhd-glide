#!/usr/bin/env bash
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
REPO_DIR="$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)"

if [ "$#" -lt 1 ]; then
  echo "usage: $0 <target-ip> [port] [h264|h265] [mp4-file] [--fast]" >&2
  echo "       $0 <target-ip> [port] --codec h264|h265 [--file mp4-file] [--fast]" >&2
  echo "example local test: $0 127.0.0.1 5600 h264" >&2
  exit 2
fi

TARGET="$1"
shift

PORT="5600"
if [ "$#" -gt 0 ]; then
  case "$1" in
    ''|*[!0-9]*)
      ;;
    *)
      PORT="$1"
      shift
      ;;
  esac
fi

CODEC="${GLIDE_TEST_CODEC:-${GLIDE_CODEC:-h264}}"
VIDEO_FILE=""
VIDEO_URL="${GLIDE_TEST_VIDEO_URL:-}"
MODE="realtime"

while [ "$#" -gt 0 ]; do
  case "$1" in
    --codec)
      shift
      if [ "$#" -eq 0 ]; then
        echo "--codec requires h264 or h265" >&2
        exit 2
      fi
      CODEC="$1"
      ;;
    --codec=*)
      CODEC="${1#--codec=}"
      ;;
    h264|H264|avc|AVC)
      CODEC="h264"
      ;;
    h265|H265|hevc|HEVC)
      CODEC="h265"
      ;;
    --file)
      shift
      if [ "$#" -eq 0 ]; then
        echo "--file requires an MP4 path" >&2
        exit 2
      fi
      VIDEO_FILE="$1"
      ;;
    --file=*)
      VIDEO_FILE="${1#--file=}"
      ;;
    --url)
      shift
      if [ "$#" -eq 0 ]; then
        echo "--url requires a video URL" >&2
        exit 2
      fi
      VIDEO_URL="$1"
      ;;
    --url=*)
      VIDEO_URL="${1#--url=}"
      ;;
    --fast|fast)
      MODE="fast"
      ;;
    *)
      VIDEO_FILE="$1"
      ;;
  esac
  shift
done

CODEC="$(printf "%s" "$CODEC" | tr '[:upper:]' '[:lower:]')"
case "$CODEC" in
  h264|avc)
    CODEC="h264"
    ENCODING_NAME="H264"
    RTP_PAY="rtph264pay"
    PARSE="h264parse"
    RAW_CAPS="video/x-h264"
    OUT_CAPS="video/x-h264,stream-format=byte-stream,alignment=au"
    DEFAULT_VIDEO_FILE="${REPO_DIR}/examples/media/battlefield_1080p_120fps_8mbps_h264.mp4"
    DEFAULT_VIDEO_URL="https://blurbusters.com/wp-content/uploads/2019/01/battlefield_1080p_120fps_8mbps.mp4"
    ;;
  h265|hevc)
    CODEC="h265"
    ENCODING_NAME="H265"
    RTP_PAY="rtph265pay"
    PARSE="h265parse"
    RAW_CAPS="video/x-h265"
    OUT_CAPS="video/x-h265,stream-format=byte-stream,alignment=au"
    DEFAULT_VIDEO_FILE="${REPO_DIR}/examples/media/battlefield_1080p_120fps_8mbps_h265.mp4"
    DEFAULT_VIDEO_URL=""
    ;;
  *)
    echo "unsupported codec '${CODEC}'; use h264 or h265" >&2
    exit 2
    ;;
esac

if [ -z "$VIDEO_FILE" ]; then
  VIDEO_FILE="$DEFAULT_VIDEO_FILE"
fi
if [ -z "$VIDEO_URL" ]; then
  VIDEO_URL="$DEFAULT_VIDEO_URL"
fi

if [ "$MODE" = "fast" ]; then
  SINK_SYNC=false
  MODE_LABEL="looping, unpaced full-speed send"
else
  SINK_SYNC=true
  MODE_LABEL="looping, realtime/timestamp-paced send"
fi

download_file() {
  if [ -z "$VIDEO_URL" ]; then
    echo "missing ${ENCODING_NAME} test video: ${VIDEO_FILE}" >&2
    echo "Pass an existing file path, or set GLIDE_TEST_VIDEO_URL / --url to a pre-encoded ${ENCODING_NAME} MP4." >&2
    exit 1
  fi

  mkdir -p "$(dirname "$VIDEO_FILE")"

  echo "Downloading ${ENCODING_NAME} test video:" >&2
  echo "  ${VIDEO_URL}" >&2
  echo "  -> ${VIDEO_FILE}" >&2

  if command -v curl >/dev/null 2>&1; then
    curl -L --fail --continue-at - --output "$VIDEO_FILE" "$VIDEO_URL"
  elif command -v wget >/dev/null 2>&1; then
    wget -c -O "$VIDEO_FILE" "$VIDEO_URL"
  else
    echo "curl or wget is required to download the test video" >&2
    exit 1
  fi
}

if [ ! -s "$VIDEO_FILE" ]; then
  download_file
fi

DISCOVER_INFO=""
if command -v gst-discoverer-1.0 >/dev/null 2>&1; then
  DISCOVER_INFO="$(gst-discoverer-1.0 "$VIDEO_FILE" 2>/dev/null || true)"
  if [ "$CODEC" = "h264" ] && printf "%s" "$DISCOVER_INFO" | grep -Eiq "H\.265|HEVC|video/x-h265"; then
    echo "selected h264, but ${VIDEO_FILE} appears to be H.265/HEVC" >&2
    exit 1
  fi
  if [ "$CODEC" = "h265" ] && printf "%s" "$DISCOVER_INFO" | grep -Eiq "H\.264|AVC|video/x-h264"; then
    echo "selected h265, but ${VIDEO_FILE} appears to be H.264/AVC" >&2
    exit 1
  fi
fi

RESTART_SECONDS="${GLIDE_STREAM_RESTART_SECONDS:-}"
if [ -z "$RESTART_SECONDS" ] && [ -n "$DISCOVER_INFO" ]; then
  DURATION_TEXT="$(printf "%s\n" "$DISCOVER_INFO" | sed -n 's/^[[:space:]]*Duration:[[:space:]]*//p' | head -n 1)"
  if [ -n "$DURATION_TEXT" ]; then
    RESTART_SECONDS="$(awk -v duration="$DURATION_TEXT" 'BEGIN {
      parts = split(duration, field, ":");
      if (parts == 3) {
        seconds = (field[1] * 3600) + (field[2] * 60) + field[3];
        if (seconds > 0) {
          printf "%d", seconds + 5;
        }
      }
    }')"
  fi
fi
if [ -z "$RESTART_SECONDS" ]; then
  RESTART_SECONDS="40"
fi
if [ "$RESTART_SECONDS" = "0" ]; then
  RESTART_SECONDS=""
fi

echo "Streaming 1080p120 ${ENCODING_NAME} file to ${TARGET}:${PORT}" >&2
echo "  file=${VIDEO_FILE}" >&2
echo "  mode=${MODE_LABEL}" >&2
echo "  parser=${PARSE}" >&2
echo "  payloader=${RTP_PAY}" >&2
echo "  restart-guard=${RESTART_SECONDS:-disabled}" >&2

GST_CMD=(
  gst-launch-1.0 -e
  filesrc "location=${VIDEO_FILE}" !
  qtdemux name=demux
  demux.video_0 !
  queue max-size-buffers=0 max-size-bytes=0 max-size-time=0 !
  "${RAW_CAPS}" !
  "${PARSE}" config-interval=1 !
  "${OUT_CAPS}" !
  "${RTP_PAY}" pt=96 config-interval=1 mtu=1200 !
  udpsink "host=${TARGET}" "port=${PORT}" "sync=${SINK_SYNC}" async=false
)

while :; do
  STATUS=0
  if [ -n "$RESTART_SECONDS" ] && command -v timeout >/dev/null 2>&1; then
    timeout -s INT "$RESTART_SECONDS" "${GST_CMD[@]}" || STATUS="$?"
  else
    "${GST_CMD[@]}" || STATUS="$?"
  fi

  case "$STATUS" in
    0)
      ;;
    124|130)
      echo "stream pass reached restart guard; restarting" >&2
      ;;
    *)
      exit "$STATUS"
      ;;
  esac
done
