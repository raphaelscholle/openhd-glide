#!/usr/bin/env sh
set -eu

TARGET="${1:-192.168.2.2}"
PORT="${2:-5600}"

exec gst-launch-1.0 -v \
  videotestsrc is-live=true pattern=smpte ! \
  "video/x-raw,width=1280,height=720,framerate=60/1" ! \
  x264enc tune=zerolatency speed-preset=ultrafast bitrate=4000 key-int-max=30 bframes=0 ! \
  h264parse config-interval=1 ! \
  rtph264pay pt=96 config-interval=1 ! \
  udpsink host="${TARGET}" port="${PORT}" sync=false async=false
