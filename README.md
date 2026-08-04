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

Connect both halves and keep them powered on. Enter Settings (see below) and tap
the top-left red corner to put the left half in bootloader mode, or the top-right
red corner for the right half, then:

```sh
make flash   # left, then right
```
Just one half: `make flash-left` / `make flash-right`. Details: `make help`, `scripts/flash.sh`.

Flashing a newly built image drops any keymap saved from ZMK Studio, so the
board always comes up running exactly what was built. Bluetooth pairings and
the rest of the settings partition are untouched; `make flash-reset` is still
the way to wipe those. See Diagnostics for how it works and how to turn it off.

## Testing

```sh
make test
```
Runs the behavior tests in `tests/` on ZMK's native simulator (timed key
events in, emitted keycodes diffed against snapshots). Currently covers the
Space/Cmd thumb keys; see `tests/space-cmd/README.md`.

## Linting

```sh
make lint
```
Runs via `mise` (`.mise.toml`); `lefthook` runs the same checks on `git commit` (wired up by `make setup`).

## Editing the keymap

- **[customkeymap.com](https://customkeymap.com/) (recommended)**: web-based ZMK keymap visualizer/editor. Point it at this repo (owner/repo or a direct `.keymap` link), click keys to change bindings/layers/behaviors/combos, and commit straight back to GitHub — or export SVG/PNG or a `.keymap` file. No install required.
- [keymap-editor](https://nickcoutsos.github.io/keymap-editor/): web app, GitHub OAuth, commits straight to this repo.
- [ZMK Studio](https://zmk.studio/download): live edit over USB (left half, studio-rpc enabled). **Saving writes to the board's settings flash, not to this repo, and those bindings then override the compiled keymap on every boot** — they last until the next firmware flash clears them (see Diagnostics), so anything worth keeping belongs in `config/tomahawk56.keymap`. Locked by default; unlock with the purple key on the Settings layer (`&studio_unlock`).
- Directly: `config/tomahawk56.keymap` (ASCII layer diagrams in comments)

## Diagnostics

If the keyboard behaves differently from `config/tomahawk56.keymap`, it is
probably running bindings pinned in settings flash by a ZMK Studio save made
since the last flash. Plug the **left** half in over USB and ask it:

```sh
make live-keymap
```

It reads the running keymap (read-only) and lists every position where the board
disagrees with `build/left/zephyr/zephyr.dts`. To clear pinned bindings:

```sh
make clear-pinned-keymap   # drops the saved keymap; keeps Bluetooth pairings
```

`make flash-reset` does the same but wipes the whole settings partition, so it
forces re-pairing.

Why this happens: `CONFIG_ZMK_STUDIO` selects
`CONFIG_ZMK_KEYMAP_SETTINGS_STORAGE`, and `keymap_handle_set()` in
`zmk/app/src/keymap.c` reapplies saved bindings over the compiled keymap at every
boot. Saved bindings store a behavior *local ID* assigned at build time, so after
an unrelated rebuild a pinned binding can even resolve to a different behavior
than the one saved.

Which is why the firmware clears them itself on the first boot after a flash
(`CONFIG_TOMAHAWK56_STUDIO_RESET_ON_FLASH`, on by default). Every build stamps a
fresh id into the image (`config/cmake/tomahawk56_build_id.cmake`); the board
records the id it last booted next to the saved keymap, and
`config/src/tomahawk56_studio_reset.c` drops that keymap whenever the two
disagree. Set `CONFIG_TOMAHAWK56_STUDIO_RESET_ON_FLASH=n` in
`config/tomahawk56.conf` to keep Studio saves across flashes instead.

## Runtime key bindings

The source-defined layout (derived from the Dygma Defy "Cruiser" layout) has
five layers:

- Hold `F` or `J` for Symbols.
- Hold `D` or `K` for Functional.
- Hold `S` or `L` for Magic.
- Chord `C+V` or `M+,` on Base for Escape with one hand.
- On an active layer, tap either Space/Cmd thumb to lock or unlock that layer;
  hold it for Cmd.
- Squeeze either half's two lower outer keys together for Settings (below).

The thumbs tap Tab, Backspace, Space, and Enter; holding them produces Ctrl,
Shift, Cmd, and Option respectively. The right Space thumb is the exception: it
is a plain Space key with no hold behavior, so it auto-repeats when held — the
left Space thumb carries the Cmd hold. All thumb holds are strictly time-based
(225 ms), with tap-preferred resolution protecting interrupted typing rolls. See
[`docs/migration_plan.md`](docs/migration_plan.md)
for all four typing-layer diagrams and the exact Defy migration.

## Settings

Everything that configures the *keyboard* rather than the computer lives on its
own layer, reached one-handed from either half by squeezing the lower two keys of
that half's outer column together — left of `A` + left of `Z`, or right of `'` +
right of `/`. Those four positions are dead on every other layer, so no typing
roll can reach either pair.

Settings is latched, not held — both hands stay free, so stepping through
profiles is just repeated taps. **Any thumb key exits**, as does the same squeeze
again. Every position the layer does not use is `&none`, so while Settings is on
the board types nothing and a stray key press cannot do damage. The per-key
colors are the legend:

| Key | Color | Does |
| --- | --- | --- |
| Top-left / top-right outer corner | red | `&bootloader` for **that half** — the only way in, and the first step of a flash |
| Next key in, both halves | orange | `&sys_reset` for that half: reboots it, keeps all settings. Fixes a wedged half or a split link that will not reconnect, without the power switch |
| Left outer, second row | purple | `&studio_unlock` (left half is the Studio half) |
| Left `Q` / `W` / `E` | yellow | RGB toggle, brightness down, brightness up — the same positions Magic uses |
| Left home row `A`–`G` | blue | Bluetooth profiles 1-5. The selected one turns **green** when its host is connected, **white** while it is still advertising |
| Left `Z` | magenta | `&bt BT_CLR` — forget the selected profile so that slot can pair again |
| Left `Z`+`X` together | magenta + red | `&bt BT_CLR_ALL` — forget every host pairing. A chord on purpose; it is not undoable |
| Right `H` / `J` | green / teal | Force output to USB / to Bluetooth |
| Right `N` / `M` | lime / light blue | Battery readout and connectivity readout on both halves |
| All eight thumbs | white | Back to Base |

Both entry pairs stay lit white while the layer is on, so the way out is always
visible. RGB is on `Q/W/E` in both places on purpose: Magic is the quick hold for
a one-off nudge, and Settings is where you sit and walk brightness up or down.

Split pairing between the halves is a separate bond from the host profiles:
`BT_CLR_ALL` does not touch it. Wiping that (and everything else in the settings
partition) is still `make flash-reset`.

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
- Settings is its own instrument panel; see the table above. Only its Bluetooth
  row is dynamic — the central redraws it on profile and connection changes.

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
