#!/usr/bin/env bash
set -eu

if [ "$#" -lt 1 ]; then
  echo "usage: $0 <target-ip> [port]" >&2
  echo "example local test: $0 127.0.0.1 5600" >&2
  exit 2
fi

TARGET="$1"
PORT="${2:-5600}"

encoder_self_test() {
  gst-launch-1.0 -q \
    videotestsrc num-buffers=2 ! \
    "video/x-raw,format=I420,width=320,height=240,framerate=10/1" ! \
    "$@" ! \
    h264parse ! \
    fakesink sync=false async=false >/dev/null 2>&1
}

select_encoder() {
  if gst-inspect-1.0 v4l2h264enc >/dev/null 2>&1 && encoder_self_test v4l2h264enc; then
    ENCODER_NAME="v4l2h264enc hardware encoder"
    ENCODER=(v4l2h264enc)
    return 0
  fi
  if gst-inspect-1.0 nvh264enc >/dev/null 2>&1 && encoder_self_test nvh264enc bitrate=4000 gop-size=30 bframes=0 zerolatency=true; then
    ENCODER_NAME="nvh264enc hardware encoder"
    ENCODER=(nvh264enc bitrate=4000 gop-size=30 bframes=0 zerolatency=true)
    return 0
  fi
  if gst-inspect-1.0 qsvh264enc >/dev/null 2>&1 && encoder_self_test qsvh264enc bitrate=4000 gop-size=30; then
    ENCODER_NAME="qsvh264enc hardware encoder"
    ENCODER=(qsvh264enc bitrate=4000 gop-size=30)
    return 0
  fi
  if gst-inspect-1.0 vaapih264enc >/dev/null 2>&1 && encoder_self_test vaapih264enc bitrate=4000 keyframe-period=30; then
    ENCODER_NAME="vaapih264enc hardware encoder"
    ENCODER=(vaapih264enc bitrate=4000 keyframe-period=30)
    return 0
  fi
  return 1
}

if [ -n "${GLIDE_FORCE_ENCODER:-}" ]; then
  ENCODER_NAME="${GLIDE_FORCE_ENCODER} forced encoder"
  read -r -a ENCODER <<< "${GLIDE_FORCE_ENCODER}"
elif select_encoder; then
  :
elif [ "${GLIDE_ALLOW_SOFTWARE_ENCODER:-0}" = "1" ]; then
  if ! encoder_self_test x264enc tune=zerolatency speed-preset=ultrafast bitrate=4000 key-int-max=30 bframes=0 byte-stream=true aud=true; then
    echo "x264enc fallback exists but failed its encode self-test." >&2
    exit 1
  fi
  ENCODER_NAME="x264enc software fallback"
  ENCODER=(x264enc tune=zerolatency speed-preset=ultrafast bitrate=4000 key-int-max=30 bframes=0 byte-stream=true aud=true)
else
  echo "No working hardware H.264 encoder found." >&2
  echo "If v4l2h264enc exists but dmesg shows bcm2835-codec ret -3, the Pi encoder driver is failing independently of Glide." >&2
  echo "Use a different sender with hardware H.264, fix the Pi encoder stack, or set GLIDE_ALLOW_SOFTWARE_ENCODER=1 only for non-performance fallback testing." >&2
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
