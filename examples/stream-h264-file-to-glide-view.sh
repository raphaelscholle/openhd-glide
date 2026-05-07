#!/usr/bin/env bash
set -eu

if [ "$#" -lt 1 ]; then
  echo "usage: $0 <target-ip> [port] [h264-mp4-file]" >&2
  echo "example: $0 127.0.0.1 5600" >&2
  exit 2
fi

TARGET="$1"
PORT="${2:-5600}"
VIDEO_FILE="${3:-examples/media/big-buck-bunny-1080p-60fps-30sec.mp4}"
VIDEO_URL="${GLIDE_TEST_VIDEO_URL:-https://github.com/chthomos/video-media-samples/raw/refs/heads/master/big-buck-bunny-1080p-60fps-30sec.mp4}"

download_file() {
  mkdir -p "$(dirname "$VIDEO_FILE")"
  echo "Downloading pre-encoded H.264 test video:" >&2
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

echo "Streaming pre-encoded H.264 file to ${TARGET}:${PORT}" >&2
echo "  file=${VIDEO_FILE}" >&2
echo "  pipeline=filesrc ! qtdemux ! h264parse ! rtph264pay ! udpsink" >&2
echo "  no videotestsrc, no encoder" >&2

exec gst-launch-1.0 -v \
  filesrc location="$VIDEO_FILE" ! \
  qtdemux name=demux \
  demux.video_0 ! \
  queue max-size-buffers=0 max-size-bytes=0 max-size-time=0 ! \
  h264parse config-interval=1 ! \
  "video/x-h264,stream-format=byte-stream,alignment=au" ! \
  rtph264pay pt=96 config-interval=1 mtu=1200 ! \
  udpsink host="$TARGET" port="$PORT" sync=true async=false
