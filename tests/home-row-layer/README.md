# Home-row layer-tap checks

These cases use the production-matched hold-taps in `tests/hold-taps.dtsi`.
Snapshots include the decision moment, so resolving on key release cannot pass
merely by producing the same final character as immediate key-down activation.

| Case | Expected behavior |
|---|---|
| 1 fast-nested-roll | after idle, an overlap activates the layer even when the layer key releases first |
| 2 deliberate-layer-chord | a target pressed at 155 ms activates the layer immediately |
| 3 prior-idle-guard | a press inside a typing burst stays a letter even when held |
| 4 immediate-cross-hand-chord | a cross-hand target activates the layer on key-down |
| 5 same-hand-nested-roll | a same-hand target also activates the layer on key-down |
| 6 same-hand-held-overlap | holding the target past the term does not delay activation |
| 7 repeat-letter-relaxed | a repeated letter inside 200 ms remains typing |
| 8 word-initial-long-dwell | a lone 150 ms press still types its letter |
| 9 held-alone-then-chord | a lone 180 ms hold resolves by timer before the target arrives |

The overlap cases intentionally document the responsiveness tradeoff. They do
not imply that every overlapping typing gesture should be treated as a chord.
`tests/keymap-interactions` checks the selected real bindings for all six layer
letters, including D + F and capitalization with the Shift thumbs.
