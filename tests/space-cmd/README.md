# Space/Cmd behavior tests

Regression tests for `safe_space_mt` (`config/tomahawk56.keymap`): Space on tap,
Cmd when a key is tapped inside the thumb hold or the tapping term wins.

Run with `make test`. Each case uses ZMK's snapshot test format
(`zmk/app/tests`): `native_sim.keymap` feeds timed key events into the firmware
built for the native simulator, output lines matching `events.patterns` are
diffed against `keycode_events.snapshot`, and any difference fails the case.
`behavior_keymap.dtsi` must mirror the real `safe_space_mt` definition.

| Case | Scenario | Expectation |
| --- | --- | --- |
| 1-fast-roll-through | space↓ A↓ space↑ A↑ in 70 ms | "space a", no Cmd |
| 2-fast-nested-chord | A tapped fully inside a space press | Cmd+A without waiting for the term |
| 3-typing-streak-long-press | space pressed 60 ms after a tap, held 600 ms | instant Space, no Cmd |
| 4-deliberate-cmd-chord | space held alone 600 ms, then A | Cmd+A chord |
| 5-tap-then-hold-repeat | tap space, re-press 60 ms later, hold | Space auto-repeat |
| 6-lazy-overlap-roll | thumb rests on space ~150 ms while A is rolled | "space a", no Cmd |
