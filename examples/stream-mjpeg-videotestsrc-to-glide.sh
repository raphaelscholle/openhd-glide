#!/usr/bin/env bash
################################################################################
# OpenHD
#
# Licensed under the GNU General Public License (GPL) Version 3.
#
# This software is provided "as-is," without warranty of any kind, express or
# implied, including but not limited to the warranties of merchantability,
# fitness for a particular purpose, and non-infringement. For details, see the
# full license in the LICENSE file provided with this source code.
#
# Non-Military Use Only:
# This software and its associated components are explicitly intended for
# civilian and non-military purposes. Use in any military or defense
# applications is strictly prohibited unless explicitly and individually
# licensed otherwise by the OpenHD Team.
#
# Contributors:
# A full list of contributors can be found at the OpenHD GitHub repository:
# https://github.com/OpenHD
#
# (C) OpenHD, All Rights Reserved.
################################################################################

set -eu

if [ "$#" -lt 1 ]; then
  echo "usage: $0 <target-ip> [port]" >&2
  echo "example local test: $0 192.168.1.94 5600" >&2
  exit 2
fi

TARGET="$1"
PORT="${2:-5600}"
WIDTH="${GLIDE_STREAM_WIDTH:-1280}"
HEIGHT="${GLIDE_STREAM_HEIGHT:-720}"
FPS="${GLIDE_STREAM_FPS:-30}"
QUALITY="${GLIDE_MJPEG_QUALITY:-85}"

for element in videotestsrc videoconvert jpegenc rtpjpegpay udpsink; do
  if ! gst-inspect-1.0 "$element" >/dev/null 2>&1; then
    echo "missing GStreamer element: $element" >&2
    exit 1
  fi
done

echo "Streaming RTP/MJPEG test video to ${TARGET}:${PORT} (${WIDTH}x${HEIGHT}@${FPS}, quality=${QUALITY})" >&2

exec gst-launch-1.0 -v \
  videotestsrc is-live=true pattern=smpte ! \
  videoconvert ! \
  "video/x-raw,format=I420,width=${WIDTH},height=${HEIGHT},framerate=${FPS}/1" ! \
  jpegenc quality="${QUALITY}" ! \
  rtpjpegpay pt=96 ! \
  udpsink host="${TARGET}" port="${PORT}" sync=false async=false
