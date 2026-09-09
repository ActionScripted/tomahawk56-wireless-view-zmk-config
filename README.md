# Tomahawk 56

ZMK firmware for a wireless Tomahawk56 with nice!view displays, per-key layer
lighting, pointing controls, ZMK Studio, and local reproducible builds. This is
a fork of
[Tomahawk-Keyboards/tomahawk56-wireless-view-zmk-config](https://github.com/Tomahawk-Keyboards/tomahawk56-wireless-view-zmk-config).

## Prerequisites and setup

Install Git, [Docker Desktop](https://www.docker.com/products/docker-desktop/)
or a compatible Docker Compose runtime, and [mise](https://mise.jdx.dev/). Start
Docker, then bootstrap the pinned tools, isolated West workspace, and Git hooks:

```sh
make setup
```

The command is safe to rerun. If the checkout predates the `.build/`-isolated
West workspace, migrate it once with `make distclean && make setup`.

## Command reference

Run `make help` for the same list at the command line.

| Command                                | Purpose                                                            |
| -------------------------------------- | ------------------------------------------------------------------ |
| `make setup`                           | Install pinned developer tools, initialize West, and install hooks |
| `make init`                            | Initialize or refresh the isolated West workspace                  |
| `make update`                          | Fetch the revisions pinned by `config/west.yml`                    |
| `make build`                           | Build `left.uf2`, `right.uf2`, and `reset.uf2`                     |
| `make build-left`                      | Build only the central/left firmware                               |
| `make build-right`                     | Build only the peripheral/right firmware                           |
| `make build-reset`                     | Build only the settings-reset image                                |
| `make keymap`                          | Generate `docs/keymap.svg` locally from the ZMK keymap             |
| `make test`                            | Run all 19 native-simulator behavior tests                         |
| `make test TEST=thumb-mod`             | Run one test suite (or provide a suite/case path)                  |
| `make lint`                            | Check C, Python, shell, and YAML files                             |
| `make format`                          | Format repository-owned C, Python, and shell files                 |
| `make clean`                           | Remove build/test output and artifacts, retaining dependencies     |
| `make distclean`                       | Remove the West workspace, dependencies, output, and artifacts     |
| `make flash`                           | Flash left and right in sequence on macOS                          |
| `make flash-left` / `make flash-right` | Flash one half on macOS                                            |
| `make flash-reset`                     | Erase all persistent settings on the connected half                |
| `make live-keymap`                     | Compare the live Studio keymap with the compiled keymap            |
| `make clear-pinned-keymap`             | Remove Studio-saved bindings without losing pairings               |

The individual lint and format targets are `lint-c`, `lint-python`,
`lint-shell`, `lint-yaml`, `format-c`, `format-python`, and `format-shell`.

## Local build workflow

Build all firmware with:

```sh
make build
```

The Make targets run `scripts/build.sh` inside the stable
`zmkfirmware/zmk-build-arm` image from `compose.yaml`. The local West calls
mirror the three entries in `build.yaml`, which the GitHub Actions workflow also
uses:

- `tomahawk56_left nice_view_adapter nice_view`, with Studio RPC over USB UART
- `tomahawk56_right nice_view_adapter nice_view`
- `settings_reset`

Fetched West projects live in `.build/west/`; generated firmware and simulator
output live elsewhere under `.build/`. Finished images are copied to:

```text
artifacts/left.uf2
artifacts/right.uf2
artifacts/reset.uf2
```

Use `make clean` for generated output only. After `make distclean`, run
`make init` or `make setup` before building again.

## Testing, linting, and formatting

```sh
make test
make lint
git diff --check
```

The simulator cases feed timed key events into ZMK and compare keycodes and
hold/tap decisions with explicit expectations. They cover home-row layer taps,
Space/Cmd, the other thumb modifiers, and interactions using selected production
bindings. Before each run, `scripts/check-test-behaviors.py` verifies the shared
behavior definitions and selected bindings against `config/tomahawk56.keymap`.
Each directory under `tests/` documents its scenarios. Golden `events.patterns`
and `keycode_events.snapshot` files should change only when behavior intentionally
changes.

Test build directories and compiler caches are retained under `.build`, so
unchanged reruns avoid pristine firmware rebuilds. During development, run a
single suite or case with `make test TEST=thumb-mod` or
`make test TEST=thumb-mod/1-fast-roll-through`. `make clean` forces the next
test run to rebuild while retaining the compiler cache.

`make lint` uses tool versions pinned in `.mise.toml`: clang-format and Ruff for
C and Python, ShellCheck and shfmt for shell, and yamllint for YAML. Lefthook
runs the matching file-specific checks before commits. Use `make format` to
apply the supported C, Python, and shell formatters.

## Flashing

### Automated macOS flashing

Keep both halves powered on. Enter Settings by squeezing the two lower keys in
either outer column, then press the red top outer corner on the half being
flashed. Run:

```sh
make flash        # left, then right
make flash-left   # left only
make flash-right  # right only
```

`scripts/flash.sh` waits for exactly one UF2 volume under `/Volumes`, attempts
to mount an enumerated but unmounted Mikoto bootloader, copies without macOS
resource forks, and waits for the board to reboot. Disconnect or unmount other
UF2 devices so the script never has to guess.

### Manual UF2 copying on any platform

1. Build the required image.
2. Connect the target half by USB and keep both halves powered.
3. Enter Settings and press that half's red top outer corner. A UF2 drive
   appears.
4. Copy `artifacts/left.uf2` to the left half or `artifacts/right.uf2` to the
   right half. Copy the file onto the mounted drive; do not rename or extract it.
5. Wait for the drive to disappear and the half to reboot before unplugging it.

The left half is the split central and stores the live keymap. On its first boot
after a normal firmware flash, it removes bindings saved by an older ZMK Studio
image while retaining Bluetooth pairings.

### Settings reset consequences

`make flash-reset` flashes `reset.uf2` and erases the entire settings partition
on that half. On the left, that includes the Studio keymap and host Bluetooth
profiles; on either half, it includes split bonding data. Re-pair whatever the
erased half forgot. A normal left/right flash or `make clear-pinned-keymap` is
safer when only Studio overrides are at fault.

## Editing the keymap

The source of truth is `config/tomahawk56.keymap`; its diagrams show every
position and layer. Editing options include:

- [customkeymap.com](https://customkeymap.com/) for a visual editor pointed at
  this repository or the raw keymap URL
- [keymap-editor](https://nickcoutsos.github.io/keymap-editor/) for a GitHub-backed
  visual editor
- [ZMK Studio](https://zmk.studio/download) for live USB editing on the left half
- direct devicetree editing for behaviors, combos, macros, and bindings

Regenerate the checked-in keymap graphic after changing the keymap:

```sh
make keymap
```

`make keymap` runs the pinned local `keymap-drawer` with the West physical
layout, the display config, and borders derived from the firmware's static
underglow maps. It writes `docs/keymap.svg`; runtime Bluetooth and output
highlights are intentionally not shown. Run `make init` first if the physical
layout is missing.

[![Generated Tomahawk56 keymap](docs/keymap.svg)](docs/keymap.svg)

ZMK Studio is locked by default. Enter Settings and press the orange outer key
on the second row to unlock it. Saving in Studio writes bindings to the board's
settings flash, not this repository, and those bindings override the compiled
keymap until cleared or superseded by the first boot of a newly built image.
Commit lasting changes to `config/tomahawk56.keymap`.

## Runtime controls

The five-layer layout is derived from Dygma Defy's Cruiser layout:

- Hold `F` or `J` for Symbols.
- Hold `D` or `K` for Functional.
- Hold `S` or `L` for Magic.
- On an active layer, tap either Space/Cmd thumb to latch or unlatch that layer;
  hold it for Cmd.
- Squeeze the lower two keys of either outer column for Settings.

Layer letters use tap-preferred resolution: release within 165 ms to type the
letter, even when another key overlaps in either release order. Holding an
eligible layer letter past 165 ms activates the layer. You can press the target
before that threshold and keep both keys held; the target is buffered until
the layer activates. This applies equally to both hands and all three layers,
including held Option+Control with `D` then `F`.

The 100 ms prior-idle guard and 200 ms repeated-letter window still force typing
for qualifying presses; holding longer cannot turn those presses into layers.
Outside those guards, a letter held past the threshold can still become a layer.

The mirrored thumbs tap Tab, Backspace, and Space; the inner left thumb taps
Escape and the inner right taps Enter. Their holds are Ctrl, Shift, Cmd, and
Option. Ctrl, Shift, and Option activate on the next key's press or after a
175 ms hold, even immediately after typing or tapping the same thumb. Release
a thumb before pressing the next key when its tap action is intended.

Shift/Backspace has an independent behavior. Tapping Backspace and immediately
holding or chording the same thumb now produces Shift, allowing a correction
followed by capitalization. For repeated deletion, tap Backspace repeatedly or
hold the plain Backspace on Functional (`D`/`K` held, then `Y`).

Space/Cmd on Base retains its typing protection: a recent typing key can force
Space, otherwise Cmd resolves when a nested key is released or after 225 ms.
Tap and re-press Space within its 200 ms quick-tap window to repeat it. On an
active layer, the same thumbs tap to toggle that layer and activate Cmd on the
next key's press or after 225 ms; a recent toggle tap does not force another
toggle.

### Settings layer

Settings is latched and blocks normal typing. Any thumb returns to Base, as does
the outer-column squeeze. Most controls are on the left half; the right retains
its bootloader corner and illuminated exit positions.

| Position                    | Color             | Action                                       |
| --------------------------- | ----------------- | -------------------------------------------- |
| Left/right top outer corner | red               | Enter that half's bootloader                 |
| `1`–`4`                     | blue              | Select Bluetooth profile 1–4                 |
| Selected profile            | green / white     | Connected / advertising                      |
| `5`                         | red               | Clear all four host pairings; irreversible   |
| Left outer key, second row  | orange            | Unlock ZMK Studio                            |
| `Q` / `W`                   | teal / light blue | Prefer USB / Bluetooth output                |
| Preferred output            | green             | Currently selected output preference         |
| `S` / `D` / `F`             | yellow            | RGB toggle / brightness down / brightness up |
| `B`                         | green             | Show battery levels on both halves           |
| Any thumb                   | white             | Return to Base                               |

`BT_CLR_ALL` clears host profiles but not the separate bond between keyboard
halves. Only `reset.uf2` erases all settings.

### Layer lighting

Each half has 24 main-key LEDs followed by four thumb LEDs. ZMK underglow owns
power and brightness; the local module supplies the per-layer frame.

- Base uses white main keys and Defy role colors on the thumbs.
- Symbols uses orange, teal, and yellow rows.
- Functional groups function, media, editing, navigation, and shortcut keys.
- Magic groups RGB, macro, mouse movement, scrolling, and mouse-button keys.
- Settings uses the control legend above and dynamically marks profiles and
  output preference.

The central synchronizes layer, brightness, and effective on/off state to the
peripheral. Battery and connection indicators render through independent
override channels. Frames update on events rather than polling, and static
frames stop the vendor underglow animation timer. RGB turns on at power-up,
dims off at idle, wakes with keyboard activity, and can be toggled off for the
current session.

## Architecture

- `config/tomahawk56.keymap` defines layers, bindings, combos, macros, the LED
  chain length, and the private split behavior node.
- `config/src/tomahawk56_layer_rgb.c` coordinates events, activity, split
  synchronization, retries, and behavior forwarding.
- `config/src/tomahawk56_layer_rgb_renderer.c` owns color maps, physical LED
  mapping, dynamic Settings indicators, rendering, and clearing.
- `config/cmake/tomahawk56_vendor_patches.cmake` applies checked patches to the
  pinned underglow and RGB-widget forks before compilation.
- `config/src/tomahawk56_studio_reset.c` removes an older Studio keymap when a
  timestamped build id changes.
- `scripts/build.sh`, `scripts/flash.sh`, and `scripts/live-keymap.py` provide
  the build, macOS flash, and read-only live-keymap interfaces.

The renderer header is private to the firmware module. Firmware-facing node
labels, compatible strings, settings keys, split payloads, and keymap bindings
are intentionally stable.

## Diagnostics and troubleshooting

If the keyboard differs from the checked-in keymap, connect the left half over
USB and run:

```sh
make live-keymap
```

The dependency-free diagnostic reads the Studio RPC UART and compares binding
parameters with `.build/firmware/left/zephyr/zephyr.dts`. It exits nonzero and
lists every overridden position when saved Studio bindings differ. To remove
only those bindings while keeping pairings:

```sh
make clear-pinned-keymap
```

Common failures:

- Missing generated devicetree: run `make build-left` before the diagnostic.
- No Studio response: use the left half, unlock Studio from Settings, and check
  that the USB cable carries data.
- Docker permission or connection errors: start Docker Desktop or the configured
  Docker-compatible runtime, then rerun the Make target.
- Missing West workspace: run `make init`.
- UF2 drive never appears: keep both halves powered, retry Settings plus the red
  corner, double-tap physical reset if accessible, and try another data cable.
- macOS sees but cannot mount the UF2 disk: unplug and retry, change port/cable,
  reboot macOS, or manually flash from Linux or Windows. Recent macOS FSKit
  versions can reject the bootloader's virtual FAT volume.
- Mouse keys fail over Bluetooth after flashing: delete and re-pair the keyboard;
  pointing support changes the HID descriptor.
- Right-side lighting does not follow the left: verify both halves are powered
  and bonded, then power-cycle both. Use `reset.uf2` only if re-bonding is needed.

The physical power switches are beside the USB-C connectors. Their marked
direction on both halves is `On ← Off`.

[ZMK documentation](https://zmk.dev/) ·
[ZMK Studio](https://zmk.studio/download) ·
[Tomahawk Keyboards FAQ](https://tomahawk-keyboards.com/pages/faq)
