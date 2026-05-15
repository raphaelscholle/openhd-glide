#!/usr/bin/env bash
set -eu

if [ "$#" -lt 1 ]; then
  echo "usage: $0 <target-ip> [port] [video-file] [--fast]" >&2
  echo "example: $0 127.0.0.1 5600" >&2
  exit 2
fi

TARGET="$1"
PORT="${2:-5600}"
VIDEO_FILE="${3:-examples/media/big-buck-bunny-1080p-60fps-30sec.mp4}"
VIDEO_URL="${GLIDE_TEST_VIDEO_URL:-https://github.com/chthomos/video-media-samples/raw/refs/heads/master/big-buck-bunny-1080p-60fps-30sec.mp4}"
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
  echo "Downloading pre-encoded test video (H.264 default):" >&2
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
  if command -v gst-discoverer-1.0 >/dev/null 2>&1; then
    local info
    info=$(gst-discoverer-1.0 "$VIDEO_FILE" 2>/dev/null || true)
    if printf "%s" "$info" | rg -q "H\.265|HEVC"; then
      CODEC="h265"
      RTP_PAY="rtph265pay"
      PARSE="h265parse"
      CAPS="video/x-h265,stream-format=byte-stream,alignment=au"
      ENCODING_NAME="H265"
      return
    fi
    if printf "%s" "$info" | rg -q "H\.264|AVC"; then
      CODEC="h264"
      RTP_PAY="rtph264pay"
      PARSE="h264parse"
      CAPS="video/x-h264,stream-format=byte-stream,alignment=au"
      ENCODING_NAME="H264"
      return
    fi
  fi
  case "${VIDEO_FILE,,}" in
    *.h265|*.hevc|*.265)
      CODEC="h265"; RTP_PAY="rtph265pay"; PARSE="h265parse"; CAPS="video/x-h265,stream-format=byte-stream,alignment=au"; ENCODING_NAME="H265";;
    *)
      CODEC="h264"; RTP_PAY="rtph264pay"; PARSE="h264parse"; CAPS="video/x-h264,stream-format=byte-stream,alignment=au"; ENCODING_NAME="H264";;
  esac
}

detect_codec

echo "Streaming pre-encoded ${ENCODING_NAME} file to ${TARGET}:${PORT}" >&2
echo "  file=${VIDEO_FILE}" >&2
echo "  pipeline=filesrc ! demux ! ${PARSE} ! ${RTP_PAY} ! udpsink" >&2
echo "  no videotestsrc, no encoder" >&2
echo "  mode=${MODE_LABEL}" >&2

while :; do
  gst-launch-1.0 -q \
    filesrc location="$VIDEO_FILE" ! \
    qtdemux name=demux \
    demux.video_0 ! \
    queue max-size-buffers=0 max-size-bytes=0 max-size-time=0 ! \
    ${PARSE} config-interval=1 ! \
    "$CAPS" ! \
    ${RTP_PAY} pt=96 config-interval=1 mtu=1200 ! \
    udpsink host="$TARGET" port="$PORT" sync="$SINK_SYNC" async=false
done
