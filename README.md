# Tomahawk 56
Fork of [Tomahawk-Keyboards/tomahawk56-wireless-view-zmk-config](https://github.com/Tomahawk-Keyboards/tomahawk56-wireless-view-zmk-config) ([ZMK](https://zmk.dev)).

## Setup

```sh
make setup   # once: mise install, west init, git hooks
```

## Build (local)

```sh
make build   # left + right + reset -> artifacts/*.uf2
```
Just one half: `make build-left` / `make build-right`. Details: `make help`, `scripts/build.sh`.

## Flash

Connect both halves and keep them powered on. Tap the top-left Base corner to
flash the left half, or the top-right Base corner to flash the right half, then:

```sh
make flash   # left, then right
```
Just one half: `make flash-left` / `make flash-right`. Details: `make help`, `scripts/flash.sh`.

## Linting

```sh
make lint
```
Runs via `mise` (`.mise.toml`); `lefthook` runs the same checks on `git commit` (wired up by `make setup`).

## Editing the keymap

- **[customkeymap.com](https://customkeymap.com/) (recommended)**: web-based ZMK keymap visualizer/editor. Point it at this repo (owner/repo or a direct `.keymap` link), click keys to change bindings/layers/behaviors/combos, and commit straight back to GitHub — or export SVG/PNG or a `.keymap` file. No install required.
- [keymap-editor](https://nickcoutsos.github.io/keymap-editor/): web app, GitHub OAuth, commits straight to this repo.
- [ZMK Studio](https://zmk.studio/download): live edit over USB (left half, studio-rpc enabled). Doesn't write back to the file.
- Directly: `config/tomahawk56.keymap` (ASCII layer diagrams in comments)

## Runtime key bindings

The source-defined Cruiser layout has four layers:

- Hold `F` or `J` for Symbols.
- Hold `D` or `K` for Functional.
- Hold `S` or `L` for Magic.
- Chord `D+F` or `J+K` on Base for Escape with one hand.
- Tap the Base key below the top-left bootloader corner to reboot with
  `&sys_reset`; it does not erase settings.
- On an active layer, tap either Space/Cmd thumb to lock or unlock that layer;
  hold it for Cmd.
- Tap the top-left or top-right Base corner to enter that half's bootloader.

The mirrored thumbs tap Tab, Backspace, Space, and Enter; holding them produces
Ctrl, Shift, Cmd, and Option respectively. Magic `Q/W/E` controls RGB toggle,
brightness down, and brightness up. See
[`tomahawk56_cruiser_phase1_migration_plan.md`](tomahawk56_cruiser_phase1_migration_plan.md)
for all four diagrams and the exact Defy migration.

## Layer lighting

The firmware uses the 28 addressable LEDs on each half as 24 main-key LEDs and
four thumb LEDs. There is no separate set of underglow-only pixels on the
Tomahawk56; ZMK's underglow controls power and dim this per-key map.
RGB is forced on at each power-up and remains on while the keyboard is awake;
the Magic-layer RGB toggle can still turn it off for the current session.

- Base lights the five active main columns on each half white and leaves both
  physical outer columns dark. The thumbs use the Defy role colors for Ctrl,
  Shift, Cmd, and Option.
- Symbols leaves the number row and outer columns dark, then lights the three
  symbol rows orange, teal, and yellow from top to bottom.
- Functional lights F1-F12 purple. Media is green; delete/editing is red;
  arrows are teal; document navigation is orange; window/navigation chords are
  blue or magenta; and application shortcuts are lime.
- Magic follows the Defy groups: RGB controls blue, macros red/orange/yellow/
  green, mouse movement teal, scrolling magenta, and mouse buttons blue/green.
  The primary/middle/secondary buttons use the Defy's distinct light blue;
  back/forward use green.

The left half synchronizes the active layer, brightness, and on/off state to
the right half over ZMK's existing split behavior transport. Battery and BLE
indicators remain on their independent RGB override channels. Layer changes,
RGB controls, split reconnections, and activity-state changes refresh the map
as events occur; the firmware does not wake periodically to poll RGB state.
For the static solid effect, each requested change renders once and stops the
underlying 20 Hz animation timer; animated underglow effects retain that timer.

> [!IMPORTANT]
> Pointing support changes the HID descriptor. Delete and re-pair the keyboard
> on Bluetooth hosts if mouse keys do not work after flashing. If ZMK Studio was
> previously used, choose **Restore Stock Settings** before testing this source
> layout, then restore split pairing if the reset cleared it.

> [!CAUTION]
> Physical power switch is located near USB-C connector. \
> Left Half, Right Half \
> On <- Off, On <- Off

[Official ZMK Studio App](https://zmk.studio/download) \
[Firmware](https://github.com/Tomahawk-Keyboards/tomahawk56-wireless-view-zmk-config/releases/download/v1.0/firmware.zip) \
[FAQ](https://tomahawk-keyboards.com/pages/faq)

<img width="1020" height="750" alt="zmk" src="https://github.com/user-attachments/assets/1e681849-774a-49cf-ab46-c0e2c19e2068" />
