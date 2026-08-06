# Thumb modifier tests

These cases cover `safe_mt`, shared by Tab/Ctrl, Backspace/Shift, and
Enter/Option. Backspace/Shift stands in for all six keys — same timing and
resolution, only the keycodes differ. Space has its own `safe_space_mt`, covered
by `tests/space-cmd/`.

`safe_mt` is tap-preferred: an interrupting key never contributes to the
decision, so the timer is the only path to the modifier, and every chord costs a
full tapping term.

| Case | Pins |
|---|---|
| 1 fast-roll-through | thumb released first is a plain tap, never a modifier |
| 2 fast-nested-roll | a letter tapped *inside* the thumb press is still a tap |
| 3 deliberate-shift-chord | the thumb held alone past the term is the modifier |
| 4 correction-burst | a re-pressed thumb outside the quick-tap window stays a tap |

Cases 2 and 4 exist because `safe_mt` was briefly balanced. It was reverted from
hardware: in a Backspace correction burst, a letter rolled inside a second
Backspace press came out capitalised after the delete, with the re-press outside
the quick-tap window. If thumb chords ever need to be faster, balanced is the
lever and these two cases are what it costs.
