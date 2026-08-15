#!/usr/bin/env bash
# Local ZMK build matching the CI matrix in build.yaml. This runs inside the
# zmk-build-arm container; fetched projects and generated output stay in .build.
set -euo pipefail

REPOSITORY_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GENERATED_ROOT="$REPOSITORY_ROOT/.build"
WEST_WORKSPACE="$GENERATED_ROOT/west"
FIRMWARE_BUILD_ROOT="$GENERATED_ROOT/firmware"
ARTIFACT_ROOT="$REPOSITORY_ROOT/artifacts"
ZMK_CONFIG_DIR="$REPOSITORY_ROOT/config"
CACHE_ROOT="$GENERATED_ROOT/cache"

# Containers are disposable, so keep compiler and Zephyr caches in .build.
export CCACHE_DIR="$CACHE_ROOT/ccache"
export XDG_CACHE_HOME="$CACHE_ROOT"

# Board and shields must remain aligned with build.yaml.
BOARD="mikoto@7.3.0//zmk"

usage() {
  echo "Usage: $0 {init|update|left|right|reset|all|test [suite[/case]]}" >&2
  exit 1
}

require_init() {
  [ -f "$WEST_WORKSPACE/.west/config" ] && [ -d "$WEST_WORKSPACE/zmk/app" ] || {
    echo "No West workspace under .build - run 'make init' first." >&2
    exit 1
  }
  cd "$WEST_WORKSPACE"
  # Each make target gets a fresh container with no Zephyr package registration.
  west zephyr-export
}

build_firmware() {
  local artifact_name="$1" shield="$2" snippet="$3"
  local build_dir="$FIRMWARE_BUILD_ROOT/$artifact_name"
  shift 3
  echo "==> Building $artifact_name"
  if [ -n "$snippet" ]; then
    west build -s zmk/app -d "$build_dir" -b "$BOARD" -S "$snippet" -- \
      -DZMK_CONFIG="$ZMK_CONFIG_DIR" -DSHIELD="$shield" "$@"
  else
    west build -s zmk/app -d "$build_dir" -b "$BOARD" -- \
      -DZMK_CONFIG="$ZMK_CONFIG_DIR" -DSHIELD="$shield" "$@"
  fi
  mkdir -p "$ARTIFACT_ROOT"
  cp "$build_dir/zephyr/zmk.uf2" "$ARTIFACT_ROOT/$artifact_name.uf2"
  echo "==> artifacts/$artifact_name.uf2"
}

command="${1:-all}"
case "$command" in
  init)
    mkdir -p "$WEST_WORKSPACE/.west"
    cd "$WEST_WORKSPACE"
    # Configure the isolated workspace directly; west init -l would place
    # .west beside the repository-owned manifest.
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
    build_firmware left "tomahawk56_left nice_view_adapter nice_view" studio-rpc-usb-uart -DCONFIG_ZMK_STUDIO=y
    ;;
  right)
    require_init
    build_firmware right "tomahawk56_right nice_view_adapter nice_view" ""
    ;;
  reset)
    require_init
    build_firmware reset settings_reset ""
    ;;
  all)
    "$0" left
    "$0" right
    "$0" reset
    ;;
  test)
    require_init
    test_path="$REPOSITORY_ROOT/tests${2:+/$2}"
    if [ ! -d "$test_path" ]; then
      echo "No test suite or case at tests${2:+/$2}." >&2
      exit 1
    fi
    ZMK_SRC_DIR=zmk/app ZMK_BUILD_DIR="$GENERATED_ROOT" \
      "$REPOSITORY_ROOT/scripts/run-tests.sh" "$test_path"
    ;;
  *) usage ;;
esac
