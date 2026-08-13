#!/usr/bin/env bash
# Copy a built UF2 to exactly one connected bootloader volume. This runs on the
# host because Docker Desktop cannot access macOS USB volumes.
#
# Bootloader mode comes from the keymap, not a button: squeeze either half's two
# lower outer keys for Settings, then tap that half's top outer corner. Layers
# resolve on the left/central half, so both halves must be on.
#
# Avoid arrays for compatibility with macOS Bash 3.2 and set -u.
set -euo pipefail
cd "$(dirname "$0")/.."

# Fallback match for the mount: macOS can expose the mountpoint before the FAT
# contents are listable.
BOOTLOADER_VOLUME_PREFIX="MIKOTO-BOOT"

is_mountpoint() {
  local directory="${1%/}"
  mount | grep -qF " on $directory ("
}

looks_like_uf2_volume() {
  local volume="$1"
  [ -f "${volume}INFO_UF2.TXT" ] && return 0
  case "$(basename "$volume")" in
    "$BOOTLOADER_VOLUME_PREFIX"*)
      # Reject stale directories left by an unclean unmount.
      is_mountpoint "$volume"
      ;;
    *) return 1 ;;
  esac
}

find_unmounted_uf2_disk() {
  diskutil list external 2>/dev/null |
    awk -v name="$BOOTLOADER_VOLUME_PREFIX" '$0 ~ name { print $NF; exit }'
}

# macOS sometimes fails to auto-mount the bootloader volume even though the USB
# disk enumerates. Mount it ourselves, rate-limited so a disk macOS simply will
# not mount is not retried every second for the whole timeout.
RESCUE_TRIES=0
RESCUE_MAX_TRIES=3

rescue_mount() {
  local disk_id
  [ "$RESCUE_TRIES" -lt "$RESCUE_MAX_TRIES" ] || return 1
  disk_id="$(find_unmounted_uf2_disk)"
  [ -n "$disk_id" ] || return 1
  RESCUE_TRIES=$((RESCUE_TRIES + 1))

  diskutil mount "$disk_id" >/dev/null 2>&1 || true
  sleep 1
  # diskutil can succeed without mounting; the mount table is authoritative.
  mount | grep -q "^/dev/${disk_id}[s ]"
}

find_single_uf2_mount() {
  local matched_mount="" match_count=0 volume

  for volume in /Volumes/*/; do
    if looks_like_uf2_volume "$volume"; then
      match_count=$((match_count + 1))
      matched_mount="$volume"
    fi
  done

  case "$match_count" in
    0)
      return 1
      ;;
    1)
      FOUND_UF2_MOUNT="$matched_mount"
      return 0
      ;;
    *)
      echo "Found $match_count UF2 drives at once - unplug/unmount all but the requested half and rerun." >&2
      return 2
      ;;
  esac
}

wait_for_uf2_mount() {
  local timeout="$1" status=0 dots=0

  for _ in $(seq 1 "$timeout"); do
    if find_single_uf2_mount; then
      status=0
    else
      status=$?
    fi
    case "$status" in
      0)
        [ "$dots" -eq 0 ] || echo
        return 0
        ;;
      1)
        if rescue_mount; then
          [ "$dots" -eq 0 ] || {
            echo
            dots=0
          }
          echo "==> Bootloader disk was attached but unmounted - mounted it manually."
          sleep 1
          continue
        fi
        # Heartbeat so a slow mount doesn't look like a hang.
        printf '.'
        dots=1
        sleep 1
        ;;
      *)
        [ "$dots" -eq 0 ] || echo
        exit "$status"
        ;;
    esac
  done

  [ "$dots" -eq 0 ] || echo
  return 1
}

copy_uf2_with_retries() {
  local uf2_path="$1" retries="$2" mount_path="" destination="" copy_error=""

  for _ in $(seq 1 "$retries"); do
    if ! wait_for_uf2_mount 1; then
      continue
    fi
    mount_path="$FOUND_UF2_MOUNT"

    destination="${mount_path}$(basename "$uf2_path")"
    echo "==> Found $mount_path - copying $uf2_path"
    # -X skips xattrs/resource forks, which the FAT bootloader drive rejects.
    if copy_error="$(cp -X "$uf2_path" "$mount_path" 2>&1)"; then
      COPIED_UF2_MOUNT="$mount_path"
      return 0
    fi

    # The bootloader reboots the instant the final UF2 block lands, yanking the
    # drive out from under cp. If the mount is gone, the board took the image.
    sleep 2
    if [ ! -d "$mount_path" ]; then
      echo "==> $mount_path vanished mid-copy - board rebooted with the new image."
      COPIED_UF2_MOUNT="$mount_path"
      return 0
    fi

    echo "==> Copy failed for $destination (${copy_error:-unknown error}); waiting for the UF2 drive to settle..."
    sleep 1
  done

  return 1
}

# macOS 15.4+ moved msdos mounting to FSKit, which sometimes misparses the
# bootloader's virtual FAT: the disk enumerates, `diskutil info` reports a
# 0-byte volume, and mounting fails. Only a mount daemon restart fixes it,
# which in practice means a reboot.
report_unmountable_disk() {
  local disk_id="$1"
  cat >&2 <<EOF
macOS sees the bootloader disk (/dev/$disk_id) but refuses to mount its filesystem.

This is a known macOS bug (FSKit's msdos module vs. the UF2 bootloader's
virtual FAT volume), not a problem with the board. Things to try, in order:

  1. Unplug the board, then plug it back in and re-enter bootloader mode
     (double-tap reset, or Settings squeeze + that half's top outer corner).
  2. Try a different USB port or cable.
  3. Reboot your Mac, then flash again before mounting any other USB drives.
  4. Flash from another machine (Linux/Windows mount these drives fine).
EOF
}

flash_one() {
  local target="$1"
  local uf2_path="artifacts/$target.uf2"
  [ -f "$uf2_path" ] || {
    echo "Missing $uf2_path - run 'make $target' first." >&2
    exit 1
  }

  case "$target" in
    left) echo "Connect the left half via USB-C, squeeze the two lower outer keys for Settings, then tap the top-left corner (red)." ;;
    right) echo "Connect the right half via USB-C, keep both halves powered on, squeeze the two lower outer keys for Settings, then tap the top-right corner (red)." ;;
    reset) echo "Connect the half you want to reset via USB-C, squeeze that half's two lower outer keys for Settings, then tap its top outer corner (red)." ;;
  esac
  echo "Waiting up to 60s for it to mount as a UF2 bootloader drive under /Volumes ..."

  local mount_path="" unmounted_disk=""
  RESCUE_TRIES=0

  if ! wait_for_uf2_mount 60; then
    unmounted_disk="$(find_unmounted_uf2_disk)"
    if [ -n "$unmounted_disk" ]; then
      report_unmountable_disk "$unmounted_disk"
    else
      echo "Timed out - no UF2 drive showed up. Is the board in bootloader mode?" >&2
    fi
    exit 1
  fi

  if ! copy_uf2_with_retries "$uf2_path" 10; then
    echo "Failed to copy $uf2_path to the UF2 drive after multiple attempts. Re-enter bootloader mode and retry." >&2
    exit 1
  fi
  mount_path="$COPIED_UF2_MOUNT"

  echo "==> Waiting for it to unmount and reboot..."
  for _ in $(seq 1 10); do
    [ -d "$mount_path" ] || break
    sleep 1
  done

  echo "==> $target done."
  # Only the central half stores a Studio keymap, which the new image drops on
  # its first boot (CONFIG_TOMAHAWK56_STUDIO_RESET_ON_FLASH).
  [ "$target" = "left" ] &&
    echo "    Its first boot drops any keymap saved from ZMK Studio. Bluetooth pairings are kept."
  return 0
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
