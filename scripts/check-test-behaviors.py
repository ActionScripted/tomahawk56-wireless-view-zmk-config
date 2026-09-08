#!/usr/bin/env python3
"""Reject simulator hold-tap fixtures that differ from the production keymap."""

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def hold_taps(path):
    """Read flat hold-tap nodes, comparing all properties independent of spacing."""
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", path.read_text(), flags=re.S)
    nodes = {}
    for label, body in re.findall(r"(\w+)\s*:\s*[\w-]+\s*\{([^{}]*)\}", text):
        properties = {re.sub(r"\s+", "", prop) for prop in body.split(";") if prop.strip()}
        if 'compatible="zmk,behavior-hold-tap"' in properties:
            nodes[label] = properties
    if not nodes:
        raise ValueError(f"No hold-taps found in {path}")
    return nodes


def main():
    production = hold_taps(REPO / "config/tomahawk56.keymap")
    fixtures = hold_taps(REPO / "tests/hold-taps.dtsi")
    mismatches = [
        label
        for label in sorted(production.keys() | fixtures.keys())
        if production.get(label) != fixtures.get(label)
    ]
    if mismatches:
        sys.exit("Simulator hold-taps differ from production: " + ", ".join(mismatches))
    print(f"Simulator hold-taps match production ({len(production)} behaviors).")
    check_interaction_bindings()


def check_interaction_bindings():
    production = (REPO / "config/tomahawk56.keymap").read_text()
    fixture = (REPO / "tests/keymap-interactions/behavior_keymap.dtsi").read_text()
    layer_defines = r"^#define\s+(L_\w+)\s+(\d+)\s*$"
    production_layers = dict(re.findall(layer_defines, production, flags=re.M))
    for name, value in re.findall(layer_defines, fixture, flags=re.M):
        if production_layers.get(name) != value:
            sys.exit(f"Interaction layer constant differs from production: {name}")
    positions = re.search(r"test-key-positions\s*=\s*<([^>]*)>", fixture)
    if positions is None:
        sys.exit("Interaction fixture has no production position mapping")
    indices = [int(value) for value in positions[1].split()]
    for layer in ("base_layer", "symbols_layer", "functional_layer"):
        bindings = []
        for source in (production, fixture):
            source = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)
            node = re.search(rf"\b{layer}\s*\{{([^{{}}]*)\}}", source)
            if node is None:
                sys.exit(f"Missing layer: {layer}")
            values = re.search(r"bindings\s*=\s*<([^>]*)>", node[1])
            if values is None:
                sys.exit(f"Missing bindings: {layer}")
            bindings.append([" ".join(b.split()) for b in re.findall(r"&[^&]+", values[1])])
        expected = [bindings[0][position] for position in indices]
        if bindings[1] != expected:
            sys.exit(f"Interaction bindings differ from production: {layer}")
    print("Interaction bindings match production (Base, Symbols, Functional).")


if __name__ == "__main__":
    main()
