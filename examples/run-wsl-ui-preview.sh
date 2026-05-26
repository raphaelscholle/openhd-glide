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
WIDTH="${GLIDE_UI_WIDTH:-760}"
HEIGHT="${GLIDE_UI_HEIGHT:-720}"
X="${GLIDE_PREVIEW_X:-80}"
Y="${GLIDE_PREVIEW_Y:-40}"
FLOW_WIDTH="${GLIDE_FLOW_WIDTH:-${GLIDE_WIDTH:-1280}}"
FLOW_HEIGHT="${GLIDE_FLOW_HEIGHT:-${GLIDE_HEIGHT:-720}}"
PORT="${GLIDE_VIEW_PORT:-5600}"
CODEC="${GLIDE_VIEW_CODEC:-${GLIDE_CODEC:-h264}}"
IPC_SOCKET="${GLIDE_IPC_SOCKET:-/tmp/openhd-glide.sock}"
TILE_ROOT="${GLIDE_MINIMAP_TILE_ROOT:-${ROOT_DIR}/assets/maps}"
MINIMAP_ZOOM="${GLIDE_MINIMAP_ZOOM:-15}"
UI_ONLY="${GLIDE_UI_ONLY:-0}"

CODEC="$(printf "%s" "$CODEC" | tr '[:upper:]' '[:lower:]')"
case "$CODEC" in
  h264|avc)
    CODEC="h264"
    ;;
  h265|hevc)
    CODEC="h265"
    ;;
  *)
    echo "unsupported codec '${CODEC}'; use h264 or h265" >&2
    exit 2
    ;;
esac

if [ "${GLIDE_SKIP_BUILD:-0}" != "1" ]; then
  if ! command -v cmake >/dev/null 2>&1; then
    echo "cmake is required to build the WSL preview; set GLIDE_SKIP_BUILD=1 to reuse existing binaries" >&2
    exit 1
  fi
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}"
  if [ "${UI_ONLY}" = "1" ]; then
    cmake --build "${BUILD_DIR}" --target glide-ui -j"$(nproc)"
  else
    cmake --build "${BUILD_DIR}" --target openhd-glide glide-view glide-flow glide-ui -j"$(nproc)"
  fi
fi

if [ "${GLIDE_SKIP_TILE_GEN:-0}" != "1" ]; then
  if command -v python3 >/dev/null 2>&1; then
    python3 "${ROOT_DIR}/scripts/generate_fake_minimap_tiles.py" --root "${TILE_ROOT}" --zoom "${MINIMAP_ZOOM}" >/dev/null
  else
    echo "python3 is required to generate fake minimap tiles; set GLIDE_SKIP_TILE_GEN=1 if tiles already exist" >&2
    exit 1
  fi
fi

if [ "${UI_ONLY}" = "1" ]; then
  REQUIRED_BINARIES="glide-ui"
else
  REQUIRED_BINARIES="openhd-glide glide-view glide-flow glide-ui"
fi

for binary in ${REQUIRED_BINARIES}; do
  if [ ! -x "${BUILD_DIR}/${binary}" ]; then
    echo "missing executable: ${BUILD_DIR}/${binary}" >&2
    exit 1
  fi
done

if command -v ldd >/dev/null 2>&1; then
  for binary in ${REQUIRED_BINARIES}; do
    LDD_OUTPUT="$(ldd "${BUILD_DIR}/${binary}" 2>&1 || true)"
    MISSING_LIBS="$(printf "%s\n" "${LDD_OUTPUT}" | sed -n '/=>.*not found/p;/Error loading shared library/p')"
    if [ -n "${MISSING_LIBS}" ]; then
      echo "${binary} is missing runtime libraries:" >&2
      echo "${MISSING_LIBS}" >&2
      exit 1
    fi
  done
fi

if [ "${UI_ONLY}" = "1" ]; then
  GLIDE_MINIMAP_TILE_ROOT="${TILE_ROOT}" \
  GLIDE_MINIMAP_ZOOM="${MINIMAP_ZOOM}" \
  exec "${BUILD_DIR}/glide-ui" \
    --preview \
    --width "${WIDTH}" \
    --height "${HEIGHT}" \
    --x "${X}" \
    --y "${Y}" \
    --ipc-socket "${IPC_SOCKET}"
fi

echo "Starting WSL preview stack with Flow OSD, LVGL UI, and View decode worker." >&2
echo "Feed video with: examples/stream-videotestsrc-to-glide-view.sh 127.0.0.1 ${PORT}" >&2

GLIDE_MINIMAP_TILE_ROOT="${TILE_ROOT}" \
GLIDE_MINIMAP_ZOOM="${MINIMAP_ZOOM}" \
exec "${BUILD_DIR}/openhd-glide" \
  --preview-stack \
  --preview-width "${FLOW_WIDTH}" \
  --flow-height "${FLOW_HEIGHT}" \
  --ui-width "${WIDTH}" \
  --ui-height "${HEIGHT}" \
  --preview-x "${X}" \
  --preview-y "${Y}" \
  --view-udp-port "${PORT}" \
  --view-udp-codec "${CODEC}" \
  --ipc-socket "${IPC_SOCKET}"
