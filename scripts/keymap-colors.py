#!/usr/bin/env python3
"""Generate keymap-drawer border CSS from the firmware layer color maps."""

import argparse
import colorsys
import re
from pathlib import Path

LAYERS = (
    ("Base", "base_main_colors", "base_thumb_colors"),
    ("Symbols", "symbols_main_colors", "base_thumb_colors"),
    ("Functional", "functional_main_colors", "base_thumb_colors"),
    ("Magic", "magic_main_colors", "base_thumb_colors"),
    ("Settings", "settings_main_colors", "settings_thumb_colors"),
)

MAIN_ROWS = 4
MAIN_COLUMNS_PER_HALF = 6
KEYMAP_COLUMNS = 12
THUMBS_PER_HALF = 4
LEFT_THUMB_START = 48
RIGHT_THUMB_START = 52


def parse_palette(source: str) -> dict[str, tuple[int, int, int]]:
    match = re.search(
        r"static const struct zmk_led_hsb color_palette\[\]\s*=\s*\{(.*?)\n\};",
        source,
        re.DOTALL,
    )
    if not match:
        raise ValueError("color_palette array not found")

    entries = re.findall(
        r"\[(COLOR_[A-Z_]+)\]\s*=\s*\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}",
        match.group(1),
    )
    if not entries:
        raise ValueError("color_palette contains no recognized entries")

    return {
        name: (int(hue), int(saturation), int(brightness))
        for name, hue, saturation, brightness in entries
    }


def split_half_definitions(source: str) -> tuple[str, str]:
    marker = "/* Rows read left-to-right as viewed on each half. */"
    start = source.find(marker)
    if start < 0:
        raise ValueError("half-specific color map marker not found")

    block = source[start + len(marker) :]
    directive = "#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)"
    left_start = block.find(directive)
    right_start = block.find("#else", left_start)
    end = block.find("#endif", right_start)
    if min(left_start, right_start, end) < 0:
        raise ValueError("half-specific color map conditional is incomplete")

    left = block[left_start + len(directive) : right_start]
    right = block[right_start + len("#else") : end]
    return left, right


def array_body(source: str, name: str) -> str:
    match = re.search(
        rf"static const uint8_t {re.escape(name)}\[[^=\n]+\]\s*=\s*\{{(.*?)\n\}};",
        source,
        re.DOTALL,
    )
    if not match:
        raise ValueError(f"{name} array not found")
    return match.group(1)


def parse_main_colors(source: str, name: str) -> list[list[str]]:
    rows = [
        re.findall(r"COLOR_[A-Z_]+", row)
        for row in re.findall(r"\{([^{}]+)\}", array_body(source, name))
    ]
    if len(rows) != MAIN_ROWS or any(len(row) != MAIN_COLUMNS_PER_HALF for row in rows):
        raise ValueError(f"{name} must contain {MAIN_ROWS} rows of {MAIN_COLUMNS_PER_HALF} colors")
    return rows


def parse_thumb_colors(source: str, name: str) -> list[str]:
    colors = re.findall(r"COLOR_[A-Z_]+", array_body(source, name))
    if len(colors) != THUMBS_PER_HALF:
        raise ValueError(f"{name} must contain {THUMBS_PER_HALF} colors")
    return colors


def hsb_to_hex(hue: int, saturation: int, brightness: int) -> str:
    red, green, blue = colorsys.hsv_to_rgb(
        (hue % 360) / 360,
        saturation / 100,
        brightness / 100,
    )
    return f"#{round(red * 255):02x}{round(green * 255):02x}{round(blue * 255):02x}"


def layer_key_colors(
    left_source: str,
    right_source: str,
    shared_source: str,
    main_array: str,
    thumb_array: str,
) -> list[str]:
    colors = ["COLOR_OFF"] * 56
    left_main = parse_main_colors(left_source, main_array)
    right_main = parse_main_colors(right_source, main_array)

    for row in range(MAIN_ROWS):
        for column in range(MAIN_COLUMNS_PER_HALF):
            colors[row * KEYMAP_COLUMNS + column] = left_main[row][column]
            colors[row * KEYMAP_COLUMNS + MAIN_COLUMNS_PER_HALF + column] = right_main[row][column]

    thumb_source_left = shared_source if thumb_array == "settings_thumb_colors" else left_source
    thumb_source_right = shared_source if thumb_array == "settings_thumb_colors" else right_source
    left_thumbs = reversed(parse_thumb_colors(thumb_source_left, thumb_array))
    right_thumbs = reversed(parse_thumb_colors(thumb_source_right, thumb_array))
    colors[LEFT_THUMB_START:RIGHT_THUMB_START] = left_thumbs
    colors[RIGHT_THUMB_START:] = right_thumbs
    return colors


def render_css(source_path: Path, source: str) -> str:
    palette = parse_palette(source)
    left_source, right_source = split_half_definitions(source)
    lines = [
        f"/* Generated from {source_path.as_posix()}; do not edit. */",
        "/* Unlit keys retain keymap-drawer's default border. */",
    ]

    for layer_name, main_array, thumb_array in LAYERS:
        key_colors = layer_key_colors(
            left_source,
            right_source,
            source,
            main_array,
            thumb_array,
        )
        positions_by_color: dict[str, list[int]] = {}
        for position, color_name in enumerate(key_colors):
            if color_name == "COLOR_OFF":
                continue
            if color_name not in palette:
                raise ValueError(f"{color_name} is not defined in color_palette")
            positions_by_color.setdefault(color_name, []).append(position)

        for color_name, positions in positions_by_color.items():
            selectors = [
                f".layer-{layer_name} .keypos-{position} rect.key" for position in positions
            ]
            lines.extend(
                f"{selector}{',' if index < len(selectors) - 1 else ''}"
                for index, selector in enumerate(selectors)
            )
            lines.extend(("{", f"  stroke: {hsb_to_hex(*palette[color_name])};"))
            if color_name == "COLOR_WHITE":
                lines.append("  filter: drop-shadow(0 0 1px #57606a);")
            lines.extend(("  stroke-width: 3px;", "}", ""))

    return "\n".join(lines).rstrip()


def yaml_overlay(css: str) -> str:
    indented_css = "\n".join(f"    {line}" if line else "" for line in css.splitlines())
    return f"draw_config:\n  svg_extra_style: |\n{indented_css}\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("renderer", type=Path, help="layer RGB renderer C source")
    parser.add_argument("output", type=Path, help="generated keymap-drawer YAML overlay")
    args = parser.parse_args()

    try:
        source = args.renderer.read_text(encoding="utf-8")
        output = yaml_overlay(render_css(args.renderer, source))
    except (OSError, ValueError) as error:
        parser.error(str(error))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output, encoding="utf-8")


if __name__ == "__main__":
    main()
