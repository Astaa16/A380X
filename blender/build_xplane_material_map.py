#!/usr/bin/env python3
"""Translate an MSFS ORM/COMP texture into an X-Plane 12 material/gloss map.

MSFS stores ambient occlusion, roughness and metallic in R/G/B. X-Plane 12's
``TEXTURE_MAP material_gloss`` expects material/F0 in red and gloss in green.
Ambient occlusion has no OBJ channel, so it is intentionally omitted.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageOps


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path, help="Decoded MSFS COMP PNG")
    parser.add_argument("output", type=Path, help="X-Plane material/gloss PNG")
    arguments = parser.parse_args()

    with Image.open(arguments.input) as source:
        red_ao, green_roughness, blue_metallic, _alpha = source.convert("RGBA").split()
        del red_ao
        gloss = ImageOps.invert(green_roughness)
        unused_blue = Image.new("L", source.size, 0)
        opaque = Image.new("L", source.size, 255)
        result = Image.merge("RGBA", (blue_metallic, gloss, unused_blue, opaque))
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        result.save(arguments.output, format="PNG", optimize=True)
    print(f"{arguments.input} -> {arguments.output}")


if __name__ == "__main__":
    main()
