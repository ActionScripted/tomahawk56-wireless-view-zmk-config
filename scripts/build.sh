#!/usr/bin/env bash
# Local ZMK build: same `west init`/`update`/`build` calls zmkfirmware/zmk's
# build-user-config.yml reusable workflow runs in CI (the workflow this repo's
# .github/workflows/build.yml calls). Invoked via `make` inside the
# zmk-build-arm container (see compose.yaml) - not meant to run bare on the
# host, since it expects `west` + the Zephyr toolchain already on PATH.
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
  # Each `make` target runs in its own throwaway container (see compose.yaml),
  # so the CMake package registration `west zephyr-export` writes under the
  # container's home directory doesn't survive between runs - only /workspace
  # (the bind-mounted repo) does. Re-run it every time; it's cheap.
  west zephyr-export
}

build() {
  local name="$1" board="$2" shield="$3" snippet="$4"
  shift 4
  echo "==> Building $name"
  if [ -n "$snippet" ]; then
    west build -s zmk/app -d "build/$name" -b "$board" -S "$snippet" -- \
      -DZMK_CONFIG="$(pwd)/config" -DSHIELD="$shield" "$@"
  else
    west build -s zmk/app -d "build/$name" -b "$board" -- \
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
    build left mikoto@7.3.0//zmk "tomahawk56_left nice_view_adapter nice_view" studio-rpc-usb-uart -DCONFIG_ZMK_STUDIO=y
    ;;
  right)
    require_init
    build right mikoto@7.3.0//zmk "tomahawk56_right nice_view_adapter nice_view" ""
    ;;
  reset)
    require_init
    build reset mikoto@7.3.0//zmk settings_reset ""
    ;;
  all)
    "$0" left
    "$0" right
    "$0" reset
    ;;
  *) usage ;;
esac
