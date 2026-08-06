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

Flashing a newly built image drops any keymap saved from ZMK Studio, so the board
always comes up running exactly what was built. Bluetooth pairings and the rest
of the settings partition are untouched; `make flash-reset` wipes those too.

## Testing

```sh
make test
```
Runs the behavior tests in `tests/` on ZMK's native simulator: timed key events
in, emitted keycodes diffed against snapshots. Covers the home-row layer-taps,
the Space/Cmd thumbs, and the other modifier thumbs; see the README in each
directory.

## Linting

```sh
make lint
```
Runs via `mise` (`.mise.toml`); `lefthook` runs the same checks on `git commit` (wired up by `make setup`).

## Editing the keymap

- **[customkeymap.com](https://customkeymap.com/) (recommended)**: web-based ZMK keymap visualizer/editor. Point it at this repo (owner/repo or a direct `.keymap` link), click keys to change bindings/layers/behaviors/combos, and commit straight back to GitHub — or export SVG/PNG or a `.keymap` file. No install required.
- [keymap-editor](https://nickcoutsos.github.io/keymap-editor/): web app, GitHub OAuth, commits straight to this repo.
- [ZMK Studio](https://zmk.studio/download): live edit over USB (left half, studio-rpc enabled). **Saving writes to the board's settings flash, not to this repo, and those bindings then override the compiled keymap on every boot** — they last until the next firmware flash clears them (see Diagnostics), so anything worth keeping belongs in `config/tomahawk56.keymap`. Locked by default; unlock with the orange key on the Settings layer (`&studio_unlock`).
- Directly: `config/tomahawk56.keymap` (ASCII layer diagrams in comments)

## Diagnostics

If the keyboard behaves differently from `config/tomahawk56.keymap`, it is
probably running bindings pinned in settings flash by a ZMK Studio save made
since the last flash. Plug the **left** half in over USB and ask it:

```sh
make live-keymap           # read-only; lists every position that disagrees with the build
make clear-pinned-keymap   # drop the saved keymap, keeping Bluetooth pairings
```

`make flash-reset` does the same but wipes the whole settings partition, so it
forces re-pairing.

The firmware also clears pinned bindings itself on the first boot after a flash
(`CONFIG_TOMAHAWK56_STUDIO_RESET_ON_FLASH`, on by default): every build stamps a
fresh id into the image, the board records the id it last booted, and
`config/src/tomahawk56_studio_reset.c` drops the saved keymap when the two
disagree. Set it to `n` in `config/tomahawk56.conf` to keep Studio saves across
flashes instead.

## Runtime key bindings

Five layers, derived from the Dygma Defy "Cruiser" layout:

- Hold `F` or `J` for Symbols.
- Hold `D` or `K` for Functional.
- Hold `S` or `L` for Magic.
- Chord `C+V` or `M+,` on Base for Escape with one hand.
- On an active layer, tap either Space/Cmd thumb to lock or unlock that layer;
  hold it for Cmd.
- Squeeze either half's two lower outer keys together for Settings (below).

The thumbs tap Tab, Backspace, Space, and Enter; holding them produces Ctrl,
Shift, Cmd, and Option respectively, mirrored on both halves. All thumbs are
tap-preferred, so no interrupting key can force a modifier — and no thumb
repeats its tap keycode by being held. To repeat Space or Backspace, tap it and
re-press within the quick-tap window.

## Settings

Everything that configures the *keyboard* rather than the computer lives on its
own layer, reached one-handed from either half by squeezing the lower two keys of
that half's outer column together — left of `A` + left of `Z`, or right of `'` +
right of `/`. Those four positions are dead on every other layer, so no typing
roll can reach either pair.

Settings is latched, not held — both hands stay free, so stepping through
profiles is just repeated taps. **Any thumb key exits**, as does the same squeeze
again. Every position the layer does not use is `&none`, so while Settings is on
the board types nothing. The per-key colors are the legend:

| Key | Color | Does |
| --- | --- | --- |
| Top-left / top-right outer corner | red | `&bootloader` for **that half** — the only way in, and the first step of a flash |
| `1`–`4` | blue | Bluetooth profiles 1-4. The selected one turns **green** when its host is connected, **white** while it is still advertising |
| `5` | red | `&bt BT_CLR_ALL` — forget all four pairings at once. Not undoable |
| Outer, second row | orange | `&studio_unlock` (left half is the Studio half) |
| `Q` / `W` | teal / light blue | Send typing over USB / over Bluetooth. Whichever is selected turns **blue** |
| `S` / `D` / `F` | yellow | RGB toggle, brightness down, brightness up |
| `B` | green | Battery readout — both halves paint a level bar, green to red |
| All eight thumbs | white | Back to Base |

Settings is a **left-handed panel**: every control is on the left half, and the
right half keeps only its own bootloader corner and the white pair that toggles
the layer. Both entry pairs stay lit white while the layer is on, so the way out
is always visible.

RGB appears both here and on Magic on purpose: Magic is the quick hold for a
one-off nudge, Settings is where you sit and walk brightness up or down. There is
no soft-reboot key — the power switch on each half already does that.

Split pairing between the halves is a separate bond from the host profiles:
`BT_CLR_ALL` does not touch it. Wiping that (and everything else in the settings
partition) is `make flash-reset`.

## Layer lighting

The firmware uses the 28 addressable LEDs on each half as 24 main-key LEDs and
four thumb LEDs. There is no separate set of underglow-only pixels on the
Tomahawk56; ZMK's underglow controls power and dim this per-key map. RGB is
forced on at each power-up and stays on while the keyboard is awake; the Magic
and Settings toggles can still turn it off for the current session.

- Base lights the five active main columns on each half white and leaves both
  physical outer columns dark. The thumbs keep the Defy role colors for Ctrl,
  Shift, Cmd, and Option, on every layer except Settings.
- Symbols leaves the number row and outer columns dark, then lights the three
  symbol rows orange, teal, and yellow from top to bottom.
- Functional lights F1-F12 purple, then groups the rest by kind: media, editing,
  arrows, document navigation, window chords, and application shortcuts.
- Magic follows the Defy groups: RGB controls blue, macros red/orange/yellow/
  green, mouse movement teal, scrolling magenta, and mouse buttons blue/green.
- Settings is its own instrument panel; see the table above. Only its Bluetooth
  and output keys are dynamic — the central redraws them on profile, connection,
  and endpoint changes.

The left half syncs the active layer, brightness, and on/off state to the right
half over ZMK's existing split behavior transport. Battery and BLE indicators
stay on their independent RGB override channels. The map refreshes on events —
layer changes, RGB controls, split reconnections, activity state — rather than by
polling, and a static frame stops the underlying 20 Hz animation timer.

> [!IMPORTANT]
> Pointing support changes the HID descriptor. Delete and re-pair the keyboard
> on Bluetooth hosts if mouse keys do not work after flashing.

> [!CAUTION]
> Physical power switch is located near USB-C connector. \
> Left Half, Right Half \
> On <- Off, On <- Off

[Official ZMK Studio App](https://zmk.studio/download) \
[Firmware](https://github.com/Tomahawk-Keyboards/tomahawk56-wireless-view-zmk-config/releases/download/v1.0/firmware.zip) \
[FAQ](https://tomahawk-keyboards.com/pages/faq)

<img width="1020" height="750" alt="zmk" src="https://github.com/user-attachments/assets/1e681849-774a-49cf-ab46-c0e2c19e2068" />
