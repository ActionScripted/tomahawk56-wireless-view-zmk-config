# Home-row layer-tap tests

These cases mirror the balanced, positional layer-taps from the production
keymap. Each one pins a distinct guard, so a timing change that trades one
away fails loudly instead of quietly.

| Case | Pins |
|---|---|
| 1 fast-nested-roll | a rolling cross-hand overlap (S released first) is typing |
| 2 deliberate-layer-chord | a chord started before the term still resolves, on the target key's release |
| 3 prior-idle-guard | a key pressed inside a typing burst cannot start a layer |
| 4 immediate-cross-hand-chord | the reported F-held → J case produces its symbol, with no wait |
| 5 same-hand-nested-roll | a same-hand neighbour, released early, forces a tap |
| 6 same-hand-held-overlap | a same-hand neighbour *still held* at the term forces a tap |
| 7 repeat-letter-relaxed | a doubled letter outside the idle window stays typing |
| 8 word-initial-long-dwell | a 200 ms dwell on a lone key still types its letter |
| 9 held-alone-then-chord | held alone past the term, the layer does engage |

Cases 6, 7 and 8 were added after an audit and all three failed on first run:

- 6 emitted the same-hand key's *symbol* and swallowed the layer key's letter.
  Cause: `hold-trigger-on-release` defers the positional check to the first key
  released, so the check was still unarmed when the term expired with the
  neighbour down. Fixed by dropping the property — with `balanced`, a rolling
  overlap is already a tap by way of the hold-tap's own key-up, so checking at
  press time costs nothing and arms the guard earlier.
- 7 turned the second letter of a doubled pair into a layer. Fixed by adding
  `quick-tap-ms`.
- 8 emitted **nothing at all** — the hold is `&mo`, which produces no keycode,
  so a dwell past the term silently eats the letter. Fixed by lengthening the
  tapping term, which case 2 shows costs a deliberate chord nothing.

Cases 8 and 9 bracket the tapping term from both sides; keep them together if
that value is ever retuned.
