# Home-row layer-tap tests

These cases mirror the balanced, positional layer-taps from the production
keymap. Each one pins a distinct guard, so a timing change that trades one away
fails loudly instead of quietly.

| Case | Pins |
|---|---|
| 1 fast-nested-roll | a rolling cross-hand overlap (S released first) is typing |
| 2 deliberate-layer-chord | a chord started before the term still resolves, on the target key's release |
| 3 prior-idle-guard | a key pressed inside a typing burst cannot start a layer |
| 4 immediate-cross-hand-chord | F held → J produces its symbol, with no wait |
| 5 same-hand-nested-roll | a same-hand neighbour, released early, forces a tap |
| 6 same-hand-held-overlap | a same-hand neighbour *still held* at the term forces a tap |
| 7 repeat-letter-relaxed | a doubled letter outside the idle window stays typing |
| 8 word-initial-long-dwell | a 150 ms dwell on a lone key still types its letter |
| 9 held-alone-then-chord | held alone past the term, the layer does engage |

Case 6 is why the behaviors must not use `hold-trigger-on-release`: it defers the
positional check to the first key released, leaving it unarmed when a same-hand
neighbour is still down as the term expires.

Cases 8 and 9 bracket the tapping term from both sides; keep them together if
that value is ever retuned.
