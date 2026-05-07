#!/usr/bin/env sh
set -eu

if [ "$#" -lt 1 ]; then
  echo "usage: $0 <target-ip> [port]" >&2
  echo "example local test: $0 127.0.0.1 5600" >&2
  exit 2
fi

TARGET="$1"
PORT="${2:-5600}"

echo "Streaming RTP/H264 test video to ${TARGET}:${PORT}" >&2

exec gst-launch-1.0 -v \
  videotestsrc is-live=true pattern=smpte ! \
  videoconvert ! \
  "video/x-raw,format=I420,width=1280,height=720,framerate=60/1" ! \
  x264enc tune=zerolatency speed-preset=ultrafast bitrate=4000 key-int-max=30 bframes=0 byte-stream=true aud=true ! \
  "video/x-h264,stream-format=byte-stream,alignment=au,profile=baseline" ! \
  h264parse config-interval=1 ! \
  rtph264pay pt=96 config-interval=1 ! \
  udpsink host="${TARGET}" port="${PORT}" sync=false async=false
