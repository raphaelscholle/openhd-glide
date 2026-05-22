#!/usr/bin/env sh
set -eu

SCRIPT_PATH="$(readlink -f "$0")"
ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "${SCRIPT_PATH}")/.." && pwd)"
BUILD_DIR="${GLIDE_BUILD_DIR:-${ROOT_DIR}/build-wsl}"
WIDTH="${GLIDE_MINIMAP_WIDTH:-420}"
HEIGHT="${GLIDE_MINIMAP_HEIGHT:-420}"
X="${GLIDE_PREVIEW_X:-920}"
Y="${GLIDE_PREVIEW_Y:-40}"
TILE_ROOT="${GLIDE_MINIMAP_TILE_ROOT:-${ROOT_DIR}/assets/maps}"
ZOOM="${GLIDE_MINIMAP_ZOOM:-15}"
GRID="${GLIDE_MINIMAP_GRID:-5}"

if [ "${GLIDE_SKIP_BUILD:-0}" != "1" ]; then
  if ! command -v cmake >/dev/null 2>&1; then
    echo "cmake is required to build glide-minimap-demo; set GLIDE_SKIP_BUILD=1 to reuse an existing binary" >&2
    exit 1
  fi
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}"
  cmake --build "${BUILD_DIR}" --target glide-minimap-demo -j"$(nproc)"
fi

if [ "${GLIDE_SKIP_TILE_GEN:-0}" != "1" ]; then
  if command -v python3 >/dev/null 2>&1; then
    python3 "${ROOT_DIR}/scripts/generate_fake_minimap_tiles.py" --root "${TILE_ROOT}" --zoom "${ZOOM}" >/dev/null
  else
    echo "python3 is required to generate fake minimap tiles; set GLIDE_SKIP_TILE_GEN=1 if tiles already exist" >&2
    exit 1
  fi
fi

if [ ! -x "${BUILD_DIR}/glide-minimap-demo" ]; then
  echo "missing executable: ${BUILD_DIR}/glide-minimap-demo" >&2
  exit 1
fi

if command -v ldd >/dev/null 2>&1; then
  LDD_OUTPUT="$(ldd "${BUILD_DIR}/glide-minimap-demo" 2>&1 || true)"
  MISSING_LIBS="$(printf "%s\n" "${LDD_OUTPUT}" | sed -n '/=>.*not found/p;/Error loading shared library/p')"
  if [ -n "${MISSING_LIBS}" ]; then
    echo "glide-minimap-demo is missing runtime libraries:" >&2
    echo "${MISSING_LIBS}" >&2
    exit 1
  fi
fi

exec "${BUILD_DIR}/glide-minimap-demo" \
  --preview \
  --width "${WIDTH}" \
  --height "${HEIGHT}" \
  --x "${X}" \
  --y "${Y}" \
  --tile-root "${TILE_ROOT}" \
  --zoom "${ZOOM}" \
  --grid "${GRID}"
