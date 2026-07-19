#!/usr/bin/env bash
# Copies a built .uf2 onto a connected UF2 bootloader drive. Bootloader mode is
# entered with a key press, not a physical button: config/tomahawk56.keymap's
# default layer binds &bootloader directly to the Left Shift and Right Shift
# positions. On the split right half, that key press is handled through the
# left/central half, so both halves must be on when using the in-keymap
# bootloader shortcut. This replicates Zephyr's own
# `west flash` uf2 runner (scripts/west_commands/runners/uf2.py): it looks for
# a FAT volume with INFO_UF2.TXT at its root, refuses to guess if more than
# one matches, and copies the file over. Runs on the host rather than through
# `make`/Docker, since Docker Desktop on macOS doesn't expose the USB
# mass-storage volume the bootloader presents.
#
# Avoids bash arrays on purpose: macOS still ships bash 3.2 by default, which
# mishandles "${arr[@]}" on an empty array under `set -u`.
set -euo pipefail
cd "$(dirname "$0")/.."

find_single_uf2_mount() {
  local mount="" match_count=0 vol

  for vol in /Volumes/*/; do
    if [ -f "${vol}INFO_UF2.TXT" ]; then
      match_count=$((match_count + 1))
      mount="$vol"
    fi
  done

  case "$match_count" in
    0)
      return 1
      ;;
    1)
      FOUND_UF2_MOUNT="$mount"
      return 0
      ;;
    *)
      echo "Found $match_count UF2 drives at once - unplug/unmount all but the requested half and rerun." >&2
      return 2
      ;;
  esac
}

wait_for_uf2_mount() {
  local timeout="$1" status=0

  for _ in $(seq 1 "$timeout"); do
    find_single_uf2_mount || status=$?
    case "$status" in
      0) return 0 ;;
      1) sleep 1 ;;
      *) exit "$status" ;;
    esac
  done

  return 1
}

copy_uf2_with_retries() {
  local uf2="$1" retries="$2" mount="" dest=""

  for _ in $(seq 1 "$retries"); do
    if ! wait_for_uf2_mount 1; then
      continue
    fi
    mount="$FOUND_UF2_MOUNT"

    dest="${mount}$(basename "$uf2")"
    echo "==> Found $mount - copying $uf2"
    if cp "$uf2" "$mount" 2>/dev/null; then
      COPIED_UF2_MOUNT="$mount"
      return 0
    fi

    echo "==> Copy failed for $dest; waiting for the UF2 drive to settle..."
    sleep 1
  done

  return 1
}

flash_one() {
  local target="$1"
  local uf2="artifacts/$target.uf2"
  [ -f "$uf2" ] || {
    echo "Missing $uf2 - run 'make $target' first." >&2
    exit 1
  }

  case "$target" in
    left) echo "Connect the left half via USB-C, then press Left Shift (single press)." ;;
    right) echo "Connect the right half via USB-C, keep both halves powered on, then press Right Shift once from the full split." ;;
    reset) echo "Connect the half you want to reset via USB-C, keep both halves powered on if targeting the right half, then press its Left/Right Shift once." ;;
  esac
  echo "Waiting up to 60s for it to mount as a UF2 bootloader drive under /Volumes ..."

  local mount=""
  if ! wait_for_uf2_mount 60; then
    echo "Timed out - no UF2 drive showed up. Is the board in bootloader mode?" >&2
    exit 1
  fi

  if ! copy_uf2_with_retries "$uf2" 10; then
    echo "Failed to copy $uf2 to the UF2 drive after multiple attempts. Re-enter bootloader mode and retry." >&2
    exit 1
  fi
  mount="$COPIED_UF2_MOUNT"

  echo "==> Waiting for it to unmount and reboot..."
  for _ in $(seq 1 10); do
    [ -d "$mount" ] || break
    sleep 1
  done

  echo "==> $target done."
}

target="${1:-}"
case "$target" in
  left | right | reset) flash_one "$target" ;;
  all)
    flash_one left
    flash_one right
    ;;
  *)
    echo "Usage: $0 {left|right|reset|all}" >&2
    exit 1
    ;;
esac
