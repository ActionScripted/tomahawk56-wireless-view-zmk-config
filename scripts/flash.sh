#!/usr/bin/env bash
# Copies a built .uf2 onto a connected UF2 bootloader drive. Bootloader mode is
# entered with a key press, not a physical button: squeeze either half's two
# lower outer keys together to turn the Settings layer on, then tap that half's
# top outer corner, which is where config/tomahawk56.keymap binds &bootloader.
# Nothing on Base reboots the board. Combos and layers are resolved on the
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

# Volume name the mikoto's UF2 bootloader mounts under (fallback match when
# INFO_UF2.TXT isn't readable yet - macOS can expose the mountpoint before the
# FAT contents are listable, which used to leave the wait loop stuck forever).
BOOTLOADER_VOLUME_PREFIX="MIKOTO-BOOT"

is_mountpoint() {
  local dir="${1%/}"
  mount | grep -qF " on $dir ("
}

looks_like_uf2_volume() {
  local vol="$1"
  [ -f "${vol}INFO_UF2.TXT" ] && return 0
  case "$(basename "$vol")" in
    "$BOOTLOADER_VOLUME_PREFIX"*)
      # Name alone isn't proof: a stale dir can linger under /Volumes after an
      # unclean unmount. Only trust it if something is actually mounted there.
      is_mountpoint "$vol"
      ;;
    *) return 1 ;;
  esac
}

find_unmounted_uf2_disk() {
  diskutil list external 2>/dev/null |
    awk -v name="$BOOTLOADER_VOLUME_PREFIX" '$0 ~ name { print $NF; exit }'
}

# macOS sometimes fails to auto-mount the bootloader's FAT volume even though
# the USB disk enumerates (visible in `diskutil list` but absent from
# /Volumes). If a disk carrying the bootloader volume name is sitting there
# unmounted, mount it ourselves. Rate-limited so a disk macOS simply won't
# mount doesn't get retried every second for the whole timeout.
RESCUE_TRIES=0
RESCUE_MAX_TRIES=3

rescue_mount() {
  local id
  [ "$RESCUE_TRIES" -lt "$RESCUE_MAX_TRIES" ] || return 1
  id="$(find_unmounted_uf2_disk)"
  [ -n "$id" ] || return 1
  RESCUE_TRIES=$((RESCUE_TRIES + 1))

  diskutil mount "$id" >/dev/null 2>&1 || true
  sleep 1
  # diskutil exits 0 even when nothing mounted, so only trust the mount table.
  mount | grep -q "^/dev/${id}[s ]"
}

find_single_uf2_mount() {
  local mount="" match_count=0 vol

  for vol in /Volumes/*/; do
    if looks_like_uf2_volume "$vol"; then
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
  local uf2="$1" retries="$2" mount="" dest="" cp_err=""

  for _ in $(seq 1 "$retries"); do
    if ! wait_for_uf2_mount 1; then
      continue
    fi
    mount="$FOUND_UF2_MOUNT"

    dest="${mount}$(basename "$uf2")"
    echo "==> Found $mount - copying $uf2"
    # -X skips xattrs/resource forks, which the FAT bootloader drive rejects.
    if cp_err="$(cp -X "$uf2" "$mount" 2>&1)"; then
      COPIED_UF2_MOUNT="$mount"
      return 0
    fi

    # The bootloader reboots the instant the final UF2 block lands, which can
    # yank the drive out from under cp and make a successful flash look like
    # an I/O error. If the mount is gone, the board took the image.
    sleep 2
    if [ ! -d "$mount" ]; then
      echo "==> $mount vanished mid-copy - board rebooted with the new image."
      COPIED_UF2_MOUNT="$mount"
      return 0
    fi

    echo "==> Copy failed for $dest (${cp_err:-unknown error}); waiting for the UF2 drive to settle..."
    sleep 1
  done

  return 1
}

# macOS 15.4+ moved msdos mounting to FSKit, which sometimes misparses the
# bootloader's virtual FAT: the disk enumerates, `diskutil info` reports a
# 0-byte volume, and mounting fails. Nothing this script can do fixes it -
# the mount daemon has to be restarted, which in practice means a reboot.
report_unmountable_disk() {
  local id="$1"
  cat >&2 <<EOF
macOS sees the bootloader disk (/dev/$id) but refuses to mount its filesystem.

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
  local uf2="artifacts/$target.uf2"
  [ -f "$uf2" ] || {
    echo "Missing $uf2 - run 'make $target' first." >&2
    exit 1
  }

  case "$target" in
    left) echo "Connect the left half via USB-C, squeeze the two lower outer keys for Settings, then tap the top-left corner (red)." ;;
    right) echo "Connect the right half via USB-C, keep both halves powered on, squeeze the two lower outer keys for Settings, then tap the top-right corner (red)." ;;
    reset) echo "Connect the half you want to reset via USB-C, squeeze that half's two lower outer keys for Settings, then tap its top outer corner (red)." ;;
  esac
  echo "Waiting up to 60s for it to mount as a UF2 bootloader drive under /Volumes ..."

  local mount="" stuck_disk=""
  RESCUE_TRIES=0

  if ! wait_for_uf2_mount 60; then
    stuck_disk="$(find_unmounted_uf2_disk)"
    if [ -n "$stuck_disk" ]; then
      report_unmountable_disk "$stuck_disk"
    else
      echo "Timed out - no UF2 drive showed up. Is the board in bootloader mode?" >&2
    fi
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
  # The image clears the saved Studio keymap on its first boot (see
  # CONFIG_TOMAHAWK56_STUDIO_RESET_ON_FLASH); only the central half stores one.
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
