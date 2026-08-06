# Thumb modifier tests

These cases mirror the shared `safe_mt` behavior used by Tab/Ctrl,
Backspace/Shift, and Enter/Option. Backspace/Shift stands in for all six keys:
the behavior's timing and resolution are identical, while the bound keycodes
are parameters.

`safe_mt` is tap-preferred, which means an interrupting key never contributes
to the decision — the timer is the only path to the modifier. That is the
strictest available policy against accidental modifiers, and it is also why
every chord costs a full 175 ms wait.

Space is deliberately *not* on this behavior; it has its own `safe_space_mt`,
covered by `tests/space-cmd/`.

| Case | Pins |
|---|---|
| 1 fast-roll-through | thumb released first is a plain tap, never a modifier |
| 2 fast-nested-roll | a letter tapped *inside* the thumb press is still a tap |
| 3 deliberate-shift-chord | the thumb held alone past 175 ms is the modifier |
| 4 correction-burst | a re-pressed thumb outside the quick-tap window stays a tap |

Cases 2 and 4 exist because `safe_mt` was briefly balanced, to make chords
resolve on the chord key's release the way the home-row layer keys do. It was
reverted from hardware: in a Backspace correction burst, a letter rolled inside
a second Backspace press came out capitalised after the delete. Case 4
reproduces that burst with the re-press 150 ms after the first — outside the
125 ms quick-tap window, which is precisely why `quick-tap-ms` could not cover
it.

If thumb chords ever need to be faster, balanced is the lever, and these two
cases are what it costs. Lengthening the term is not — that only makes
deliberate chords slower without changing either case.

Neither policy lets a held thumb auto-repeat its tap keycode; that is inherent
to a mod-tap. Tap and re-press inside `quick-tap-ms` to repeat.
