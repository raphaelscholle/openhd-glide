#!/usr/bin/env bash
set -eu

if [ "$#" -lt 1 ]; then
  echo "usage: $0 <target-ip> [port] [mp4-file] [--fast]" >&2
  echo "example local test: $0 127.0.0.1 5600" >&2
  exit 2
fi

TARGET="$1"
PORT="${2:-5600}"
VIDEO_FILE="${3:-examples/media/battlefield_1080p_120fps_8mbps_h265.mp4}"
VIDEO_URL="${GLIDE_TEST_VIDEO_URL:-https://blurbusters.com/wp-content/uploads/2019/01/battlefield_1080p_120fps_8mbps.mp4}"
MODE="${4:-realtime}"

if [ "$MODE" = "--fast" ] || [ "$MODE" = "fast" ]; then
  SINK_SYNC=false
  MODE_LABEL="looping, unpaced full-speed send"
else
  SINK_SYNC=true
  MODE_LABEL="looping, realtime/timestamp-paced send"
fi

download_file() {
  mkdir -p "$(dirname "$VIDEO_FILE")"

  echo "Downloading 1080p120 test video:" >&2
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

detect_codec() {
  RTP_PAY=""
  PARSE=""
  CAPS=""
  ENCODING_NAME=""

  if command -v gst-discoverer-1.0 >/dev/null 2>&1; then
    info="$(gst-discoverer-1.0 "$VIDEO_FILE" 2>/dev/null || true)"

    if printf "%s" "$info" | grep -Eiq "H\.265|HEVC|video/x-h265"; then
      RTP_PAY="rtph265pay"
      PARSE="h265parse"
      CAPS="video/x-h265,stream-format=byte-stream,alignment=au"
      ENCODING_NAME="H265"
      return
    fi

    if printf "%s" "$info" | grep -Eiq "H\.264|AVC|video/x-h264"; then
      RTP_PAY="rtph264pay"
      PARSE="h264parse"
      CAPS="video/x-h264,stream-format=byte-stream,alignment=au"
      ENCODING_NAME="H264"
      return
    fi
  fi

  case "${VIDEO_FILE,,}" in
    *.h265|*.hevc|*.265)
      RTP_PAY="rtph265pay"
      PARSE="h265parse"
      CAPS="video/x-h265,stream-format=byte-stream,alignment=au"
      ENCODING_NAME="H265"
      ;;
    *)
      RTP_PAY="rtph264pay"
      PARSE="h264parse"
      CAPS="video/x-h264,stream-format=byte-stream,alignment=au"
      ENCODING_NAME="H264"
      ;;
  esac
}

detect_codec

echo "Streaming 1080p120 ${ENCODING_NAME} file to ${TARGET}:${PORT}" >&2
echo "  file=${VIDEO_FILE}" >&2
echo "  mode=${MODE_LABEL}" >&2
echo "  parser=${PARSE}" >&2
echo "  payloader=${RTP_PAY}" >&2

while :; do
  gst-launch-1.0 -e -q \
    filesrc location="$VIDEO_FILE" ! \
    qtdemux name=demux \
      demux. ! queue max-size-buffers=0 max-size-bytes=0 max-size-time=0 ! \
        "$CAPS" ! \
        ${PARSE} config-interval=1 ! \
        ${RTP_PAY} pt=96 config-interval=1 mtu=1200 ! \
        udpsink host="$TARGET" port="$PORT" sync="$SINK_SYNC" async=false \
      demux. ! queue ! fakesink sync=false
done