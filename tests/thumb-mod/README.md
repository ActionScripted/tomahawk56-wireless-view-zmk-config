# Thumb modifier tests

These cases cover `safe_mt`, shared by Tab/Ctrl, Backspace/Shift, and
Enter/Option. Backspace/Shift stands in for all six keys — same timing and
resolution, only the keycodes differ. Space has its own `safe_space_mt`, covered
by `tests/space-cmd/`.

`safe_mt` is balanced: a key pressed and released while the thumb remains down
resolves the modifier immediately, without waiting for the tapping term. A roll
where the thumb is released before the letter remains a tap.

| Case | Pins |
|---|---|
| 1 fast-roll-through | thumb released first is a plain tap, never a modifier |
| 2 fast-nested-chord | a letter tapped *inside* the thumb press gets Shift |
| 3 timer-resolved-shift | a thumb held alone past the term is also the modifier |
| 4 correction-burst | documents the fast-chord tradeoff after a Backspace re-press |

Case 4 documents the tradeoff: in a Backspace correction burst, a letter rolled
fully inside a second Backspace press is treated as an intentional Shift chord
when the re-press falls outside the quick-tap window. Releasing Backspace before
the letter preserves the normal lowercase typing roll covered by case 1.
