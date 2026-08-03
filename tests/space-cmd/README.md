# Space/Cmd behavior tests

Regression tests for `safe_space_mt` (`config/tomahawk56.keymap`): Space on
tap, Cmd only by holding past the 300 ms tapping term. They pin the property
that fast typing can never misfire Cmd.

Run them with `make test`. Each case uses ZMK's snapshot test format
(`zmk/app/tests`): `native_sim.keymap` feeds timed key events into the real
firmware built for the native simulator, output lines matching
`events.patterns` are compared against `keycode_events.snapshot`, and any
diff fails the case. `behavior_keymap.dtsi` here must mirror the
`safe_space_mt` definition in the real keymap.

| Case | Scenario | Expectation |
| --- | --- | --- |
| 1-fast-roll-through | space↓ A↓ space↑ A↑ in 70 ms | "space a", no Cmd |
| 2-fast-roll-nested | A tapped fully inside a space press | "space a", no Cmd |
| 3-typing-streak-long-press | space pressed 60 ms after a tap, held 400 ms | instant Space, no Cmd |
| 4-deliberate-cmd-chord | space held alone 400 ms, then A | Cmd+A chord |
| 5-tap-then-hold-repeat | tap space, re-press within 175 ms, hold | Space auto-repeat |
