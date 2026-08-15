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
- Sources of truth are `config/tomahawk56.keymap` for bindings,
  `keymap_drawer.config.yaml` for custom labels, glyphs, and styling, and
  `config/src/tomahawk56_layer_rgb_renderer.c` for colored borders.
- The drawing config is human-maintained only for display customization; normal
  ZMK bindings parse automatically. Its raw binding keys may use keymap macros
  and layer constants because keymap-drawer preprocesses them.
- `scripts/keymap-colors.py` generates `.build/keymap/colors.yaml`; do not
  duplicate renderer color maps in the drawing config or commit `.build/keymap`.
- `make keymap` needs the West physical layout. Run `make init` if it is missing.

## Firmware structure

- `config/src/tomahawk56_layer_rgb.c` coordinates layer RGB events, split
  synchronization, and behavior forwarding.
- `config/src/tomahawk56_layer_rgb_renderer.c` owns physical LED mapping and
  rendered frames.
- Keep firmware-facing node labels, compatible strings, settings keys, split
  payloads, and keymap bindings stable unless a task explicitly requires a
  compatibility change.
