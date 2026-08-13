#!/usr/bin/env bash
# Local ZMK build, mirroring the west calls that build-user-config.yml makes in
# CI. Run via `make` inside the zmk-build-arm container (see compose.yaml); it
# expects `west` and the Zephyr toolchain already on PATH. All West projects
# and generated build/test output live under .build/ so the repository root
# stays limited to source files and finished artifacts.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
GENERATED_DIR="$ROOT_DIR/.build"
WEST_WORKSPACE="$GENERATED_DIR/west"
FIRMWARE_BUILD_DIR="$GENERATED_DIR/firmware"
ARTIFACT_DIR="$ROOT_DIR/artifacts"
CONFIG_DIR="$ROOT_DIR/config"

usage() {
  echo "Usage: $0 {init|update|left|right|reset|all|test}" >&2
  exit 1
}

require_init() {
  [ -f "$WEST_WORKSPACE/.west/config" ] && [ -d "$WEST_WORKSPACE/zmk/app" ] || {
    echo "No West workspace under .build - run 'make init' first." >&2
    exit 1
  }
  cd "$WEST_WORKSPACE"
  # Each `make` target gets a throwaway container, and zephyr-export writes its
  # CMake package registration outside /workspace. Cheap, so just redo it.
  west zephyr-export
}

# Board and shields must match build.yaml (the CI matrix).
BOARD="mikoto@7.3.0//zmk"

build() {
  local name="$1" shield="$2" snippet="$3"
  local build_dir="$FIRMWARE_BUILD_DIR/$name"
  shift 3
  echo "==> Building $name"
  if [ -n "$snippet" ]; then
    west build -s zmk/app -d "$build_dir" -b "$BOARD" -S "$snippet" -- \
      -DZMK_CONFIG="$CONFIG_DIR" -DSHIELD="$shield" "$@"
  else
    west build -s zmk/app -d "$build_dir" -b "$BOARD" -- \
      -DZMK_CONFIG="$CONFIG_DIR" -DSHIELD="$shield" "$@"
  fi
  mkdir -p "$ARTIFACT_DIR"
  cp "$build_dir/zephyr/zmk.uf2" "$ARTIFACT_DIR/$name.uf2"
  echo "==> artifacts/$name.uf2"
}

cmd="${1:-all}"
case "$cmd" in
  init)
    mkdir -p "$WEST_WORKSPACE/.west"
    cd "$WEST_WORKSPACE"
    # `west init -l` always puts .west next to the local manifest repository.
    # Configure this isolated workspace directly so the tracked config/ can
    # remain at the repository root while every fetched project lives here.
    west config --local manifest.path ../../config
    west config --local manifest.file west.yml
    west config --local zephyr.base zephyr
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
  test)
    require_init
    ZMK_SRC_DIR=zmk/app ZMK_BUILD_DIR="$GENERATED_DIR" \
      zmk/app/run-test.sh "$ROOT_DIR/tests"
    ;;
  *) usage ;;
esac
