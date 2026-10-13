#!/usr/bin/env python3
"""Decode selected FBW interior materials for XPlane2Blender.

The source glTF and source KTX2 files remain unchanged. Generated PNGs have a
bounded resolution; manifests resolve texture indices through MSFT_texture_dds
instead of assuming that texture and image indices are interchangeable.
"""

from __future__ import annotations

import argparse
import io
import json
import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import unquote

from PIL import Image

from convert_ktx2_textures import ktx2_to_dds


def safe_name(value: str) -> str:
    return re.sub(r"[^a-z0-9_]+", "_", value.lower()).strip("_")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--asset-root", required=True, type=Path)
    parser.add_argument("--texture-dir", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--prefix", required=True)
    parser.add_argument("--max-size", type=int, default=2048)
    parser.add_argument("--detail-max-size", type=int, help="Optional smaller limit for normal and COMP maps")
    parser.add_argument("--include-material", action="append", default=[], help="Select a material name by regular expression (repeatable)")
    parser.add_argument("--allow-transparent-material", action="append", default=[], help="Allow a named label/decal regex to retain MASK/BLEND alpha (repeatable)")
    parser.add_argument("--polygon-offset", type=int, default=0, help="X-Plane depth bias for explicitly selected label/decal surfaces")
    parser.add_argument("--reuse-decoded", action="store_true", help="Reuse existing decoded image-NNN PNG intermediates in this texture directory")
    args = parser.parse_args()
    if args.max_size < 1 or args.max_size & (args.max_size - 1):
        raise ValueError("--max-size must be a positive power of two")
    if args.detail_max_size is not None and (args.detail_max_size < 1 or args.detail_max_size & (args.detail_max_size - 1)):
        raise ValueError("--detail-max-size must be a positive power of two")
    document = json.loads(args.source.read_text())
    args.texture_dir.mkdir(parents=True, exist_ok=True)
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    index: dict[str, list[Path]] = {}
    for path in args.asset_root.rglob("*.KTX2"):
        if not path.name.startswith("._"):
            index.setdefault(path.name.casefold(), []).append(path)

    converted: dict[int, Path] = {}
    texture_report = []

    def image_for(texture_index: int) -> tuple[int, Path]:
        texture = document["textures"][texture_index]
        source_index = texture.get("extensions", {}).get("MSFT_texture_dds", {}).get("source", texture.get("source"))
        if source_index is None:
            raise ValueError(f"No source image for texture {texture_index}")
        image = document["images"][source_index]
        uri = unquote(image["uri"]).replace("\\", "/")
        filename = Path(uri).name
        direct_candidates = [args.source.parent / uri, args.source.parent.parent / "texture" / filename]
        source_path = next((p for p in direct_candidates if p.is_file()), None)
        if source_path is None:
            candidates = index.get(filename.casefold(), [])
            common = [p for p in candidates if "/common/texture/" in p.as_posix().casefold()]
            if len(candidates) == 1:
                source_path = candidates[0]
            elif len(common) == 1:
                source_path = common[0]
            else:
                raise FileNotFoundError(f"Missing or ambiguous texture {filename}: {candidates}")
        return source_index, source_path

    def convert(texture_index: int, detail: bool = False) -> Path:
        source_index, source_path = image_for(texture_index)
        if source_index in converted:
            return converted[source_index]
        destination = args.texture_dir / f"image-{source_index:03d}.png"
        if args.reuse_decoded and destination.is_file():
            with Image.open(destination) as image:
                dimensions = list(image.size)
            converted[source_index] = destination
            texture_report.append({"source_index": source_index, "source": str(source_path), "output": str(destination), "output_size": dimensions, "bytes": destination.stat().st_size, "reused_decoded": True})
            return destination
        dds, info = ktx2_to_dds(source_path)
        with Image.open(io.BytesIO(dds)) as source_image:
            image = source_image.convert("RGBA")
            max_size = min(args.max_size, args.detail_max_size or args.max_size) if detail else args.max_size
            image.thumbnail((max_size, max_size), Image.Resampling.LANCZOS)
            image.save(destination, optimize=True)
            dimensions = list(image.size)
        converted[source_index] = destination
        texture_report.append({"source_index": source_index, "source": str(source_path), "output": str(destination), "source_size": [info["width"], info["height"]], "output_size": dimensions, "bytes": destination.stat().st_size})
        print(f"Decoded {source_path.name} -> {destination.name} {dimensions}", flush=True)
        return destination

    exports, excluded = [], []
    for material in document.get("materials", []):
        name = material["name"]
        extensions = material.get("extensions", {})
        if name.upper() in {"SUNBLOCKER", "COLLISION", "A380_PORTHOLE_BLOCKER"}:
            excluded.append({"name": name, "reason": "MSFS shadow/collision helper; not a visible aircraft surface"})
            continue
        if args.include_material and not any(re.search(pattern, name) for pattern in args.include_material):
            excluded.append({"name": name, "reason": "outside selected material subset"})
            continue
        alpha_mode = material.get("alphaMode", "OPAQUE")
        allowed_transparent = any(re.search(pattern, name) for pattern in args.allow_transparent_material)
        if (alpha_mode != "OPAQUE" and not allowed_transparent) or "INVIS" in name.upper() or "ASOBO_material_invisible" in extensions:
            excluded.append({"name": name, "reason": "transparent or invisible MSFS material requires separate integration"})
            continue
        variant = {"name": f"{args.prefix}_{safe_name(name)}", "materials": [name], "alpha_mode": alpha_mode, "alpha_cutoff": material.get("alphaCutoff", 0.5), "double_sided": material.get("doubleSided", False), "polygon_offset": args.polygon_offset}
        pbr = material.get("pbrMetallicRoughness", {})
        albedo = pbr.get("baseColorTexture")
        if albedo:
            variant["texture"] = str(convert(albedo["index"]))
            factors = pbr.get("baseColorFactor", [1, 1, 1, 1])
            if any(abs(float(factor) - 1.0) > 1e-7 for factor in factors):
                # OBJ8 does not supply glTF's per-material baseColorFactor.
                # Bake it in linear light; keeping separate output names is
                # essential when white and black labels share the same atlas.
                def tint_lut(factor: float) -> list[int]:
                    table = []
                    for value in range(256):
                        srgb_value = value / 255
                        linear = srgb_value / 12.92 if srgb_value <= 0.04045 else ((srgb_value + 0.055) / 1.055) ** 2.4
                        linear *= float(factor)
                        tinted = 12.92 * linear if linear <= 0.0031308 else 1.055 * linear ** (1 / 2.4) - 0.055
                        table.append(round(255 * max(0, min(1, tinted))))
                    return table
                destination = args.texture_dir / f"{safe_name(name)}_albedo.png"
                with Image.open(variant["texture"]) as source_image:
                    channels = list(source_image.convert("RGBA").split())
                    for channel in range(3):
                        channels[channel] = channels[channel].point(tint_lut(factors[channel]))
                    channels[3] = channels[3].point([round(value * float(factors[3])) for value in range(256)])
                    Image.merge("RGBA", channels).save(destination, optimize=True)
                variant["texture"] = str(destination)
                variant["baked_base_color_factor"] = factors
        else:
            # A texture is still required so an untextured opaque floor or sun
            # visor preserves its authored color without sharing another atlas.
            color = pbr.get("baseColorFactor", [1, 1, 1, 1])
            def srgb(v: float) -> int:
                return round(255 * (12.92 * v if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055))
            rgba = tuple(srgb(float(v)) for v in color[:3]) + (255,)
            destination = args.texture_dir / f"{safe_name(name)}_albedo.png"
            Image.new("RGBA", (4, 4), rgba).save(destination)
            variant["texture"] = str(destination)
        if material.get("normalTexture"):
            variant["texture_map_normal"] = str(convert(material["normalTexture"]["index"], detail=True))
        if pbr.get("metallicRoughnessTexture"):
            comp = convert(pbr["metallicRoughnessTexture"]["index"], detail=True)
            gloss = args.texture_dir / f"{safe_name(name)}_material_gloss.png"
            subprocess.run([sys.executable, str(Path(__file__).with_name("build_xplane_material_map.py")), str(comp), str(gloss)], check=True)
            variant["texture_map_material_gloss"] = str(gloss)
        if material.get("emissiveTexture"):
            if name == "A380X_COCKPIT_DECAL_EMIS":
                variant["texture_lit"] = str(convert(material["emissiveTexture"]["index"]))
                variant["lit_level_dataref"] = "a380x/xp12/lighting/integral"
                variant["decal_offset_m"] = 0.002
                variant["decal_cutout_ratio"] = 0.15
                # Physical separation allows normal depth writes; polygon
                # offset here can let subsequently drawn panels erase text.
                variant["polygon_offset"] = 0
            else:
                variant["emissive_source_pending"] = material["emissiveTexture"]["index"]
        exports.append(variant)

    args.manifest.write_text(json.dumps({"exports": exports}, indent=2) + "\n")
    args.report.write_text(json.dumps({"source": str(args.source), "max_texture_size": args.max_size, "detail_max_texture_size": args.detail_max_size or args.max_size, "selected_materials": len(exports), "transparent_material_allowlist": args.allow_transparent_material, "excluded_materials": excluded, "decoded_images": texture_report, "manifest": str(args.manifest), "limitations": ["Opaque surfaces plus explicitly allowlisted labels/decals only.", "MSFS emission, transparent glass, clickspots and specialized shaders require later integration.", "Normal maps retain the source tangent-map convention used by the existing exterior conversion."]}, indent=2) + "\n")
    print(f"Prepared {len(exports)} materials, {len(converted)} decoded images; omitted {len(excluded)} materials", flush=True)


if __name__ == "__main__":
    main()
