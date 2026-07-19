#!/usr/bin/env bash
# Copies a built .uf2 onto a connected UF2 bootloader drive. Bootloader mode is
# entered with a key press, not a physical button: config/tomahawk56.keymap's
# default layer binds &bootloader directly to the Left Shift and Right Shift
# keys (those keys no longer type Shift). This replicates Zephyr's own
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

flash_one() {
  local target="$1"
  local uf2="artifacts/$target.uf2"
  [ -f "$uf2" ] || {
    echo "Missing $uf2 - run 'make $target' first." >&2
    exit 1
  }

  case "$target" in
    left) echo "Connect the left half via USB-C, then press Left Shift (single press)." ;;
    right) echo "Connect the right half via USB-C, then press Right Shift (single press)." ;;
    reset) echo "Connect the half you want to reset via USB-C, then press its Left/Right Shift (single press)." ;;
  esac
  echo "Waiting up to 60s for it to mount as a UF2 bootloader drive under /Volumes ..."

  local mount="" match_count=0 vol
  for _ in $(seq 1 60); do
    match_count=0
    for vol in /Volumes/*/; do
      if [ -f "${vol}INFO_UF2.TXT" ]; then
        match_count=$((match_count + 1))
        mount="$vol"
      fi
    done
    [ "$match_count" -gt 0 ] && break
    sleep 1
  done

  case "$match_count" in
    0)
      echo "Timed out - no UF2 drive showed up. Is the board in bootloader mode?" >&2
      exit 1
      ;;
    1) ;;
    *)
      echo "Found $match_count UF2 drives at once - unplug/unmount all but the $target half and rerun." >&2
      exit 1
      ;;
  esac

  echo "==> Found $mount - copying $uf2"
  cp "$uf2" "$mount"

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
