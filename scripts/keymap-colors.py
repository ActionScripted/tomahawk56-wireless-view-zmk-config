#!/usr/bin/env python3
"""Generate keymap-drawer borders from the firmware's static underglow maps."""

import colorsys
import re
from pathlib import Path
from textwrap import indent

ROOT = Path(__file__).resolve().parents[1]
RENDERER = Path("config/src/tomahawk56_layer_rgb_renderer.c")
OUTPUT = Path(".build/keymap/colors.yaml")
LAYERS = (
    ("Base", "base", "base"),
    ("Symbols", "symbols", "base"),
    ("Functional", "functional", "base"),
    ("Magic", "magic", "base"),
    ("Settings", "settings", "settings"),
)
COLOR_RE = r"COLOR_[A-Z_]+"


def array(source: str, name: str) -> str:
    return re.search(rf"\b{name}\b[^=]*=\s*\{{(.*?)\n\}};", source, re.DOTALL).group(1)


def colors(source: str, name: str) -> list[str]:
    return re.findall(COLOR_RE, array(source, name))


def render_css(source: str) -> str:
    palette = {
        name: tuple(map(int, hsb))
        for name, *hsb in re.findall(
            rf"\[({COLOR_RE})\]\s*=\s*\{{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}}",
            array(source, "color_palette"),
        )
    }

    maps = source.split("/* Rows read left-to-right as viewed on each half. */", 1)[1]
    left, right = maps.split("#else", 1)
    right = right.split("#endif", 1)[0]
    blocks = []

    for layer, main_map, thumb_map in LAYERS:
        main_name = f"{main_map}_main_colors"
        left_main = colors(left, main_name)
        right_main = colors(right, main_name)
        key_colors = [
            color
            for start in range(0, 24, 6)
            for color in left_main[start : start + 6] + right_main[start : start + 6]
        ]

        thumb_name = f"{thumb_map}_thumb_colors"
        left_thumbs = colors(left if thumb_name in left else source, thumb_name)
        right_thumbs = colors(right if thumb_name in right else source, thumb_name)
        key_colors += list(reversed(left_thumbs)) + list(reversed(right_thumbs))

        for color in dict.fromkeys(key_colors):
            if color == "COLOR_OFF":
                continue
            positions = [position for position, value in enumerate(key_colors) if value == color]
            selectors = ",\n".join(
                f".layer-{layer} .keypos-{position} rect.key" for position in positions
            )
            hue, saturation, brightness = palette[color]
            rgb = colorsys.hsv_to_rgb(hue / 360, saturation / 100, brightness / 100)
            hex_color = "#" + "".join(f"{round(channel * 255):02x}" for channel in rgb)
            shadow = "  filter: drop-shadow(0 0 1px #57606a);\n" if color == "COLOR_WHITE" else ""
            blocks.append(
                f"{selectors}\n{{\n  stroke: {hex_color};\n{shadow}  stroke-width: 3px;\n}}"
            )

    header = (
        f"/* Generated from {RENDERER.as_posix()}; do not edit. */\n"
        "/* Unlit keys retain keymap-drawer's default border. */"
    )
    return header + "\n" + "\n\n".join(blocks)


def main() -> None:
    source = (ROOT / RENDERER).read_text()
    css = render_css(source)
    output = "draw_config:\n  svg_extra_style: |\n" + indent(css, "    ") + "\n"
    (ROOT / OUTPUT).parent.mkdir(parents=True, exist_ok=True)
    (ROOT / OUTPUT).write_text(output)


if __name__ == "__main__":
    main()
