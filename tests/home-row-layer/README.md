# Home-row layer-tap checks

These cases use the production-matched hold-taps in `tests/hold-taps.dtsi`.
Snapshots include the decision moment. Layer holds must resolve by timer;
quick overlaps must remain typing regardless of which key releases first.

| Case | Expected behavior |
|---|---|
| 1 fast-nested-roll | after idle, a quick overlap types when the layer key releases first |
| 2 deliberate-layer-chord | a target pressed at 155 ms waits for layer activation at 165 ms |
| 3 prior-idle-guard | a press inside a typing burst stays a letter even when held |
| 4 immediate-cross-hand-chord | a quick cross-hand sequence types even when the target releases first |
| 5 same-hand-nested-roll | a quick same-hand sequence also types when the target releases first |
| 6 same-hand-held-overlap | an early same-hand target does not veto activation at the hold threshold |
| 7 repeat-letter-relaxed | a repeated letter inside 200 ms remains typing |
| 8 word-initial-long-dwell | a lone 150 ms press still types its letter |
| 9 held-alone-then-chord | a lone 170 ms hold resolves by timer before the target arrives |

Both hands use the same 165 ms threshold. Early targets are buffered, so held
chords work without a pause before the target, but quick letter overlaps do not
activate layers. A press held beyond the threshold can still activate a layer.
`tests/keymap-interactions` checks the selected real bindings for all six layer
letters, including D + F and capitalization with the Shift thumbs.
