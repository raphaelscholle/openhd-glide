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
