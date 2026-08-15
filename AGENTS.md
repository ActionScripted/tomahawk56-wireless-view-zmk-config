# Repository guidance

## Source of truth

- `config/tomahawk56.keymap` is the source of truth for bindings, layers,
  combos, macros, and custom key behaviors.
- Do not edit downloaded projects or generated files under `.build/`.
- Preserve unrelated working-tree changes. In particular, do not overwrite
  keymap edits that were already present when starting a task.

## Setup and common commands

- Run `make setup` for the initial mise, West, and Git-hook setup.
- Run `make build` to build all firmware images.
- Run `make test` for the native-simulator behavior tests.
- Run `make lint` and `git diff --check` before handing off changes.
- Behavior changes should include or update native-simulator coverage when
  practical. Do not update snapshots unless the behavior change is intended.

## Generated keymap graphic

- Run `make keymap` after changing `config/tomahawk56.keymap` and commit the
  resulting `docs/keymap.svg` when its contents change.
- `make keymap` requires the West workspace because it reads the Tomahawk56
  physical layout from `.build/west/`. Run `make setup` or `make init` first if
  that layout is missing.
- `.build/keymap/tomahawk56.yaml` is an intermediate generated file and must
  not be committed. The generated color-style overlay beside it is also build
  output.
- `scripts/keymap-colors.py` derives the SVG's colored key borders from the
  palette and static per-layer maps in
  `config/src/tomahawk56_layer_rgb_renderer.c`. Keep the renderer as the color
  source of truth; do not duplicate its color maps in the drawing config.
- `keymap_drawer.config.yaml` is intentionally human-maintained, but normal
  key, layer, hold-tap, combo, and standard ZMK behavior changes are parsed
  automatically and do not require updates to it.
- Update `keymap_drawer.config.yaml` only when adding or renaming a custom/local
  behavior, changing a raw binding that has a friendly display mapping, or
  changing the graphic's labels or styling. Do not generate this configuration
  from another script; that would merely move the same display-label mapping to
  a less transparent source.

## Firmware structure

- `config/src/tomahawk56_layer_rgb.c` coordinates layer RGB events, split
  synchronization, and behavior forwarding.
- `config/src/tomahawk56_layer_rgb_renderer.c` owns physical LED mapping and
  rendered frames.
- Keep firmware-facing node labels, compatible strings, settings keys, split
  payloads, and keymap bindings stable unless a task explicitly requires a
  compatibility change.
