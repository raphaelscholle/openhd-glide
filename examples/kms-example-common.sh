#!/usr/bin/env bash

glide_example_enabled() {
  case "$(printf '%s' "${1:-}" | tr '[:upper:]' '[:lower:]')" in
    1|true|yes|on|enabled)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

glide_example_connected_mode() {
  for status in /sys/class/drm/card*-*/status; do
    [ -r "$status" ] || continue
    [ "$(cat "$status" 2>/dev/null)" = "connected" ] || continue
    modes="${status%/status}/modes"
    [ -r "$modes" ] || continue
    mode="$(sed -n '1p' "$modes" 2>/dev/null || true)"
    case "$mode" in
      *x*)
        printf '%s\n' "$mode"
        return 0
        ;;
    esac
  done
  return 1
}

glide_example_dimension_is_auto() {
  case "$(printf '%s' "${1:-}" | tr '[:upper:]' '[:lower:]')" in
    ""|0|auto|native|preferred|best)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

glide_resolve_example_dimensions() {
  GLIDE_EXAMPLE_MODE=""
  if glide_example_dimension_is_auto "${WIDTH:-}" || glide_example_dimension_is_auto "${HEIGHT:-}"; then
    GLIDE_EXAMPLE_MODE="$(glide_example_connected_mode || true)"
  fi
  if glide_example_dimension_is_auto "${WIDTH:-}"; then
    if [ -n "$GLIDE_EXAMPLE_MODE" ]; then
      WIDTH="${GLIDE_EXAMPLE_MODE%x*}"
    else
      WIDTH=0
    fi
  fi
  if glide_example_dimension_is_auto "${HEIGHT:-}"; then
    if [ -n "$GLIDE_EXAMPLE_MODE" ]; then
      HEIGHT="${GLIDE_EXAMPLE_MODE#*x}"
    else
      HEIGHT=0
    fi
  fi
}

glide_prepare_kms_example_service() {
  GLIDE_KMS_EXAMPLE_STOPPED_SERVICE=0
  GLIDE_KMS_EXAMPLE_SERVICE="${GLIDE_KMS_SERVICE:-openhd-glide.service}"

  if ! command -v systemctl >/dev/null 2>&1; then
    return 0
  fi
  if ! systemctl is-active --quiet "$GLIDE_KMS_EXAMPLE_SERVICE"; then
    return 0
  fi
  if ! glide_example_enabled "${GLIDE_STOP_SERVICE_FOR_EXAMPLE:-1}"; then
    echo "${GLIDE_KMS_EXAMPLE_SERVICE} is active and already owns DRM/KMS." >&2
    echo "Stop it first, or run with GLIDE_STOP_SERVICE_FOR_EXAMPLE=1." >&2
    return 1
  fi

  echo "Stopping ${GLIDE_KMS_EXAMPLE_SERVICE} so this KMS example can own DRM/KMS." >&2
  sudo "${SUDO_ARGS[@]}" systemctl stop "$GLIDE_KMS_EXAMPLE_SERVICE"
  GLIDE_KMS_EXAMPLE_STOPPED_SERVICE=1
}

glide_restore_kms_example_service() {
  if [ "${GLIDE_KMS_EXAMPLE_STOPPED_SERVICE:-0}" != "1" ]; then
    return 0
  fi

  echo "Restarting ${GLIDE_KMS_EXAMPLE_SERVICE}." >&2
  sudo "${SUDO_ARGS[@]}" systemctl start "$GLIDE_KMS_EXAMPLE_SERVICE" || true
  GLIDE_KMS_EXAMPLE_STOPPED_SERVICE=0
}
