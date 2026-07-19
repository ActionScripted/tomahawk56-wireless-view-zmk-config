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

Connect a half via USB-C, press **Left Shift** (left half) / **Right Shift** (right half) to enter bootloader mode, then:

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

> [!IMPORTANT]
> Changing Bluetooth profile/switching between devices: PgDn + 1/2/3/4/5 \
> Erasing Bluetooth profile: PgDn + ESC \
> Bluetooth Backlight statuses: Solid 🔵 - connected, blinking 🔵 - open/advertising, blinking 🔴 - disconnected \
> Battery level: PgDn + Ins (Solid 🟢 - above 80%, solid 🟡 - above 20%, solid 🔴 - below 20%, 🟣 - not detected)

Default bindings:
- Lower layer: PgDn + any key
- Upper layer: PgUp + any key
- Toggle RGB On/Off: PgDn + 6
- Hue Up: PgDn + 7
- Hue Down: PgDn + U
- Saturation Up: PgDn + 8
- Saturation Down: PgDn + I
- Brightness Up: PgDn + 9
- Brightness Down: PgDn + O (letter)
- Speed Up: PgDn + 0
- Speed Down: PgDn + P
- Next Effect: PgDn + DEL
- Previous Effect: PgDn + -
- Enter bootloader mode (for flashing): Left Shift (left half) / Right Shift (right half) — these keys no longer type Shift

> [!CAUTION]
> Physical power switch is located near USB-C connector. \
> Left Half, Right Half \
> On <- Off, On <- Off

[Official ZMK Studio App](https://zmk.studio/download) \
[Firmware](https://github.com/Tomahawk-Keyboards/tomahawk56-wireless-view-zmk-config/releases/download/v1.0/firmware.zip) \
[FAQ](https://tomahawk-keyboards.com/pages/faq)

<img width="1020" height="750" alt="zmk" src="https://github.com/user-attachments/assets/1e681849-774a-49cf-ab46-c0e2c19e2068" />
