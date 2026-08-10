#!/usr/bin/env bash
# Local ZMK build, mirroring the west calls that build-user-config.yml makes in
# CI. Run via `make` inside the zmk-build-arm container (see compose.yaml); it
# expects `west` and the Zephyr toolchain already on PATH.
set -euo pipefail
cd "$(dirname "$0")/.."

usage() {
  echo "Usage: $0 {init|update|left|right|reset|all}" >&2
  exit 1
}

require_init() {
  [ -d .west ] || {
    echo "No .west workspace - run 'make init' first." >&2
    exit 1
  }
  # Each `make` target gets a throwaway container, and zephyr-export writes its
  # CMake package registration outside /workspace. Cheap, so just redo it.
  west zephyr-export
}

# Board and shields must match build.yaml (the CI matrix).
BOARD="mikoto@7.3.0//zmk"

build() {
  local name="$1" shield="$2" snippet="$3"
  shift 3
  echo "==> Building $name"
  if [ -n "$snippet" ]; then
    west build -s zmk/app -d "build/$name" -b "$BOARD" -S "$snippet" -- \
      -DZMK_CONFIG="$(pwd)/config" -DSHIELD="$shield" "$@"
  else
    west build -s zmk/app -d "build/$name" -b "$BOARD" -- \
      -DZMK_CONFIG="$(pwd)/config" -DSHIELD="$shield" "$@"
  fi
  mkdir -p artifacts
  cp "build/$name/zephyr/zmk.uf2" "artifacts/$name.uf2"
  echo "==> artifacts/$name.uf2"
}

cmd="${1:-all}"
case "$cmd" in
  init)
    [ -d .west ] || west init -l config
    west update --fetch-opt=--filter=tree:0
    west zephyr-export
    ;;
  update)
    require_init
    west update --fetch-opt=--filter=tree:0
    ;;
  left)
    require_init
    build left "tomahawk56_left nice_view_adapter nice_view" studio-rpc-usb-uart -DCONFIG_ZMK_STUDIO=y
    ;;
  right)
    require_init
    build right "tomahawk56_right nice_view_adapter nice_view" ""
    ;;
  reset)
    require_init
    build reset settings_reset ""
    ;;
  all)
    "$0" left
    "$0" right
    "$0" reset
    ;;
  *) usage ;;
esac
