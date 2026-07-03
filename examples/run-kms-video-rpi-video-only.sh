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

DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

export GLIDE_WIDTH="${GLIDE_WIDTH:-auto}"
export GLIDE_HEIGHT="${GLIDE_HEIGHT:-auto}"
export GLIDE_DISPLAY_HZ="${GLIDE_DISPLAY_HZ:-0}"
export GLIDE_LIBGL_DRIVERS_PATH="${GLIDE_LIBGL_DRIVERS_PATH:-/usr/lib/aarch64-linux-gnu/dri}"

exec "${DIR}/examples/run-kms-video-gstreamer-video-only.sh" "${1:-5600}" "${2:-${GLIDE_VIEW_CODEC:-${GLIDE_CODEC:-h264}}}"
