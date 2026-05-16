#!/usr/bin/env sh
set -eu

SCRIPT_PATH="$(readlink -f "$0")"
ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "${SCRIPT_PATH}")/.." && pwd)"
BUILD_DIR="${GLIDE_BUILD_DIR:-${ROOT_DIR}/build-wsl}"
WIDTH="${GLIDE_UI_WIDTH:-760}"
HEIGHT="${GLIDE_UI_HEIGHT:-720}"
X="${GLIDE_PREVIEW_X:-80}"
Y="${GLIDE_PREVIEW_Y:-40}"
IPC_SOCKET="${GLIDE_IPC_SOCKET:-/tmp/openhd-glide-ui-preview.sock}"

if [ ! -x "${BUILD_DIR}/glide-ui" ]; then
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}"
  cmake --build "${BUILD_DIR}" --target glide-ui -j"$(nproc)"
fi

exec "${BUILD_DIR}/glide-ui" \
  --preview \
  --width "${WIDTH}" \
  --height "${HEIGHT}" \
  --x "${X}" \
  --y "${Y}" \
  --ipc-socket "${IPC_SOCKET}"
