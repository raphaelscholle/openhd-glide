#!/usr/bin/env bash
set -eu

if [ "$#" -lt 1 ]; then
  echo "usage: $0 <target-ip> [port]" >&2
  echo "example local test: $0 127.0.0.1 5600" >&2
  exit 2
fi

TARGET="$1"
PORT="${2:-5600}"

if gst-inspect-1.0 v4l2h264enc >/dev/null 2>&1; then
  ENCODER_NAME="v4l2h264enc hardware encoder"
  ENCODER=(v4l2h264enc)
elif gst-inspect-1.0 nvh264enc >/dev/null 2>&1; then
  ENCODER_NAME="nvh264enc hardware encoder"
  ENCODER=(nvh264enc bitrate=4000 gop-size=30 bframes=0 zerolatency=true)
elif gst-inspect-1.0 qsvh264enc >/dev/null 2>&1; then
  ENCODER_NAME="qsvh264enc hardware encoder"
  ENCODER=(qsvh264enc bitrate=4000 gop-size=30)
elif gst-inspect-1.0 vaapih264enc >/dev/null 2>&1; then
  ENCODER_NAME="vaapih264enc hardware encoder"
  ENCODER=(vaapih264enc bitrate=4000 keyframe-period=30)
elif [ "${GLIDE_ALLOW_SOFTWARE_ENCODER:-0}" = "1" ]; then
  ENCODER_NAME="x264enc software fallback"
  ENCODER=(x264enc tune=zerolatency speed-preset=ultrafast bitrate=4000 key-int-max=30 bframes=0 byte-stream=true aud=true)
else
  echo "No hardware H.264 encoder found. Install/enable v4l2h264enc, nvh264enc, qsvh264enc, or vaapih264enc." >&2
  echo "Set GLIDE_ALLOW_SOFTWARE_ENCODER=1 only for non-performance fallback testing." >&2
  exit 1
fi

echo "Streaming RTP/H264 test video to ${TARGET}:${PORT} using ${ENCODER_NAME}" >&2

exec gst-launch-1.0 -v \
  videotestsrc is-live=true pattern=smpte ! \
  videoconvert ! \
  "video/x-raw,format=I420,width=1280,height=720,framerate=60/1" ! \
  "${ENCODER[@]}" ! \
  h264parse config-interval=1 ! \
  "video/x-h264,stream-format=byte-stream,alignment=au" ! \
  rtph264pay pt=96 config-interval=1 ! \
  udpsink host="${TARGET}" port="${PORT}" sync=false async=false
