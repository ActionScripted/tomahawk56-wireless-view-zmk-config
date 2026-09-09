# Keymap interaction checks

This 5x4 layout selects real production positions. Before running, the checker
compares every selected Base, Symbols and Functional binding to the keymap,
and all six shared hold-tap definitions to their production counterparts.
Magic only represents layer activation and the transparent/toggle thumbs;
its mouse actions and application macros are not simulated here.

| Case | Expected behavior |
|---|---|
| 1 held-df | D + F activates held Option+Control at the hold threshold and releases cleanly in either order |
| 2 all-layer-letters | K, F, J, S and L activate by hold duration; D is covered above |
| 3 shift-apostrophes | recent typing does not block either Shift thumb; a following apostrophe is unshifted |
| 4 rapid-ctrl-option | both Ctrl and Option thumbs can tap, then immediately chord after typing |
| 5 symbols-cmd-toggle | either thumb can latch Symbols, immediately chord Cmd, then unlatch |
| 6 functional-cmd-toggle | the same sequence works on Functional |
| 7 magic-cmd-toggle | the same sequence works on Magic |
| 8 backspace-then-held-shift | a rapid Backspace re-press held alone becomes Shift, not repeating deletion |
| 9 capital-layer-letters | all six home-row letters capitalize with Shift without activating a layer |
| 10 fast-typing-rolls | dr and same/cross-hand rolls after idle type normally for all six layer letters |
| 11 fast-nested-typing | those sequences also type when the target releases before the layer letter |
| 12 overlapping-layer-letters | pairs of layer letters type normally in both release orders |

Snapshots check key events, layer changes, and hold-tap decision moments. The
held-modifier and capitalization cases also check the resulting HID modifier
bitmap, including clearing Shift before a subsequent plain apostrophe.
Simulator success does not establish hardware latency, split-radio timing, or
whether this policy suits every natural typing overlap.
