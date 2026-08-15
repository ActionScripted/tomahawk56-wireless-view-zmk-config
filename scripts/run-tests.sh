#!/usr/bin/env bash
# Run ZMK native-simulator tests while retaining safe incremental build output.
set -uo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 <test suite or case path>" >&2
  exit 1
fi

path="$1"
build_root="${ZMK_BUILD_DIR:-${ZMK_SRC_DIR:-.}/build}"
results_log="$build_root/tests/pass-fail.log"
mkdir -p "$build_root/tests"

if [ ! -f "$path/native_sim.keymap" ]; then
  testcases=()
  while IFS= read -r -d '' keymap; do
    testcases+=("$(dirname "$keymap")")
  done < <(find "$path" -name native_sim.keymap -print0)

  if [ "${#testcases[@]}" -eq 0 ]; then
    echo "No native-simulator tests found under $path." >&2
    exit 1
  fi

  : >"$results_log"
  status=0
  printf '%s\0' "${testcases[@]}" | xargs -0 -n 1 -P "${J:-4}" "$0" || status=$?
  sort -k2 "$results_log"
  exit "$status"
fi

absolute_path="$(realpath "$path")"
testcase="${absolute_path##*/tests/}"
echo "Running $testcase:"

build_dir="$build_root/tests/$testcase"
build_signature="source=${ZMK_SRC_DIR:-};config=$absolute_path;modules=${ZMK_EXTRA_MODULES:-}"
if [ -f "$path/extra-cmake-args" ]; then
  build_signature+=";extra=$(<"$path/extra-cmake-args")"
fi

# CMake arguments force reconfiguration; otherwise let Ninja rebuild only
# tracked inputs that changed since this case last ran.
if [ -f "$build_dir/.test-build-signature" ] &&
  [ "$(<"$build_dir/.test-build-signature")" = "$build_signature" ]; then
  build_cmd=(west build -d "$build_dir")
else
  build_cmd=(west build)
  if [ -n "${ZMK_SRC_DIR:-}" ]; then
    build_cmd+=(-s "$ZMK_SRC_DIR")
  fi
  build_cmd+=(
    -d "$build_dir"
    -b native_sim//zmk_test_mock
    --pristine=auto
    --
    -DCONFIG_ASSERT=y
    "-DZMK_CONFIG=$absolute_path"
  )
  if [ -n "${ZMK_EXTRA_MODULES:-}" ]; then
    build_cmd+=("-DZMK_EXTRA_MODULES=$(realpath "$ZMK_EXTRA_MODULES")")
  fi
  if [ -f "$path/extra-cmake-args" ]; then
    # ZMK defines this file as whitespace-separated CMake arguments.
    while read -r -a extra_args; do
      build_cmd+=("${extra_args[@]}")
    done <"$path/extra-cmake-args"
  fi
fi

build_log_tmp="$build_root/tmp/$testcase/build.log"
build_log="$build_dir/build.log"
mkdir -p "$(dirname "$build_log_tmp")"
if ! "${build_cmd[@]}" >"$build_log_tmp" 2>&1; then
  mv "$build_log_tmp" "$build_log"
  rmdir -p "$(dirname "$build_log_tmp")" 2>/dev/null || true
  echo "FAILED: $testcase did not build (see $build_log)" | tee -a "$results_log"
  exit 1
fi
mv "$build_log_tmp" "$build_log"
rmdir -p "$(dirname "$build_log_tmp")" 2>/dev/null || true
printf '%s\n' "$build_signature" >"$build_dir/.test-build-signature"

"$build_dir/zephyr/zmk.exe" |
  sed -e 's/.*> //' |
  tee "$build_dir/keycode_events_full.log" |
  sed -n -f "$path/events.patterns" >"$build_dir/keycode_events.log"

if ! diff -auZ "$path/keycode_events.snapshot" "$build_dir/keycode_events.log"; then
  if [ -f "$path/pending" ]; then
    echo "PENDING: $testcase" | tee -a "$results_log"
    exit 0
  fi

  if [ -n "${ZMK_TESTS_AUTO_ACCEPT:-}" ]; then
    echo "Auto-accepting failure for $testcase"
    cp "$build_dir/keycode_events.log" "$path/keycode_events.snapshot"
  else
    echo "FAILED: $testcase" | tee -a "$results_log"
    exit 1
  fi
fi

echo "PASS: $testcase" | tee -a "$results_log"
