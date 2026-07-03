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
# © OpenHD, All Rights Reserved.
################################################################################

set -eu

SCRIPT_PATH="$(readlink -f "$0")"
ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "${SCRIPT_PATH}")/.." && pwd)"
BUILD_DIR="${GLIDE_BUILD_DIR:-${ROOT_DIR}/build-wsl}"
WIDTH="${GLIDE_FLOW_WIDTH:-${GLIDE_WIDTH:-1280}}"
HEIGHT="${GLIDE_FLOW_HEIGHT:-${GLIDE_HEIGHT:-720}}"
X="${GLIDE_PREVIEW_X:-80}"
Y="${GLIDE_PREVIEW_Y:-40}"
IPC_SOCKET="${GLIDE_IPC_SOCKET:-/tmp/openhd-glide-flow-preview.sock}"

if [ "${GLIDE_SKIP_BUILD:-0}" != "1" ]; then
  if ! command -v cmake >/dev/null 2>&1; then
    echo "cmake is required to build glide-flow; set GLIDE_SKIP_BUILD=1 to reuse an existing binary" >&2
    exit 1
  fi
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}"
  cmake --build "${BUILD_DIR}" --target glide-flow -j"$(nproc)"
fi

if [ ! -x "${BUILD_DIR}/glide-flow" ]; then
  echo "missing executable: ${BUILD_DIR}/glide-flow" >&2
  exit 1
fi

if command -v ldd >/dev/null 2>&1; then
  LDD_OUTPUT="$(ldd "${BUILD_DIR}/glide-flow" 2>&1 || true)"
  MISSING_LIBS="$(printf "%s\n" "${LDD_OUTPUT}" | sed -n '/=>.*not found/p;/Error loading shared library/p')"
  if [ -n "${MISSING_LIBS}" ]; then
    echo "glide-flow is missing runtime libraries:" >&2
    echo "${MISSING_LIBS}" >&2
    exit 1
  fi
fi

exec "${BUILD_DIR}/glide-flow" \
  --preview \
  --width "${WIDTH}" \
  --height "${HEIGHT}" \
  --x "${X}" \
  --y "${Y}" \
  --ipc-socket "${IPC_SOCKET}"
