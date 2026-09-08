# Shift/Backspace checks

These cases exercise the independent `shift_mt` behavior. Its settings are
checked against production before the simulator runs. Snapshots include the
decision moment to distinguish key-down activation from delayed output.

| Case | Expected behavior |
|---|---|
| 1 fast-roll-through | Shift activates on the letter's press even if the thumb releases first |
| 2 fast-nested-roll | a nested letter gets Shift immediately on key-down |
| 3 deliberate-shift-chord | a thumb held alone past 175 ms activates Shift |
| 4 correction-burst | tap Backspace, re-press after 60 ms, then chord Shift+A |

Backspace repetition requires separate taps or Functional-layer Backspace.
`tests/keymap-interactions` additionally covers both thumbs, recent typing,
apostrophes, home-row capitalization, and Ctrl/Option tap-to-chord transitions.
Base Space/Cmd retains its separate policy in `tests/space-cmd`.
