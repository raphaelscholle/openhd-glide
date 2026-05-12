#!/usr/bin/env bash
set -eu

if [ "$#" -lt 1 ]; then
  echo "usage: $0 <target-ip> [port] [mp4-file] [--fast]" >&2
  echo "example local test: $0 127.0.0.1 5600" >&2
  exit 2
fi

TARGET="$1"
PORT="${2:-5600}"
VIDEO_FILE="${3:-examples/media/battlefield_1080p_120fps_8mbps.mp4}"
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
  echo "Downloading 1080p120 H.264 test video:" >&2
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

echo "Streaming Blur Busters 1080p120 H.264 file to ${TARGET}:${PORT}" >&2
echo "  file=${VIDEO_FILE}" >&2
echo "  pipeline=filesrc ! qtdemux ! h264parse ! rtph264pay ! udpsink" >&2
echo "  mode=${MODE_LABEL}" >&2

while :; do
  gst-launch-1.0 -q \
    filesrc location="$VIDEO_FILE" ! \
    qtdemux name=demux \
    demux.video_0 ! \
    queue max-size-buffers=0 max-size-bytes=0 max-size-time=0 ! \
    h264parse config-interval=1 ! \
    "video/x-h264,stream-format=byte-stream,alignment=au" ! \
    rtph264pay pt=96 config-interval=1 mtu=1200 ! \
    udpsink host="$TARGET" port="$PORT" sync="$SINK_SYNC" async=false
done
