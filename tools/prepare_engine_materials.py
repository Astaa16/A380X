#!/usr/bin/env python3
"""Restore the five engine materials' authored PBR response in X-Plane 12.

Keep albedo and UVs intact. Convert DirectX normal Y to X-Plane tangent Y.
MSFS metallic becomes X-Plane F0
with a dielectric floor of .04; gloss is one minus source roughness. Enable
NORMAL_METALNESS and unit gloss modulation so these maps actually take effect.
The right core has no COMP asset: retain its glTF default m=1, roughness=1.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import sys

from PIL import Image, ImageOps

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "blender"))
from convert_ktx2_textures import ktx2_to_dds

ENGINE_NAMES = {
    "a380x_eng_lh", "a380x_eng_rh", "a380x_eng_cores_lh",
    "a380x_eng_cores_rh", "a380x_fans",
}
F0_FLOOR = round(.04 * 255)
NACELLE_NORMAL_STRENGTH = .3


def material_map(image, metallic=1.0, roughness=1.0):
    """Decode data channels linearly; never apply an sRGB transfer to ORM."""
    _, rough, metal, _ = image.convert("RGBA").split()
    f0 = metal.point([max(F0_FLOOR, round(v * metallic)) for v in range(256)])
    gloss = rough.point([255 - round(v * roughness) for v in range(256)])
    return Image.merge("RGBA", (f0, gloss, Image.new("L", image.size, 0),
                                 Image.new("L", image.size, 255)))


def normal_map(image, strength=1.0):
    """Convert DirectX Y and bound tangent XY for smooth painted cowling.

    X-Plane reconstructs positive Z from RG. Scaling XY preserves a unit
    tangent vector while reducing the source's excessive broad surface dents.
    """
    if not 0 <= strength <= 1:
        raise ValueError("Normal strength must be between zero and one")
    r, g, b = image.convert("RGB").split()
    scale = [round(127.5 + strength * (value - 127.5)) for value in range(256)]
    return Image.merge("RGB", (r.point(scale), ImageOps.invert(g).point(scale), b))


def patch_object(text, gloss_path, normal_path=None, albedo_path=None):
    """Change only material directives; preserve geometry and animation bytes."""
    lines = text.splitlines(keepends=True)
    if not any(line.split()[:2] == ["TEXTURE_MAP", "normal"] for line in lines):
        raise ValueError("Engine object lacks its required normal map")
    output = []
    for line in lines:
        field = line.split()
        if not field:
            output.append(line)
            continue
        if field[0] in {"NORMAL_METALNESS", "GLOBAL_specular"}:
            continue
        if field[:2] == ["TEXTURE_MAP", "material_gloss"]:
            continue
        if field[0] == "ATTR_shiny_rat":
            line = line[:len(line) - len(line.lstrip())] + "ATTR_shiny_rat\t1\n"
        if field[:2] == ["TEXTURE_MAP", "normal"] and normal_path:
            line = f"TEXTURE_MAP normal {normal_path}\n"
        if field[0] == "TEXTURE" and albedo_path:
            line = f"TEXTURE {albedo_path}\n"
        output.append(line)
        if field[:2] == ["TEXTURE_MAP", "normal"]:
            output.extend([f"TEXTURE_MAP material_gloss {gloss_path}\n",
                           "NORMAL_METALNESS\n", "GLOBAL_specular 1\n"])
    return "".join(output)


def save_bytes(path, content, backup, changes):
    old = path.read_bytes() if path.exists() else None
    if old == content:
        return
    saved = None
    if old is not None:
        digest = hashlib.sha256(old).hexdigest()
        saved = backup / (digest[:16] + "-" + path.name)
        saved.parent.mkdir(parents=True, exist_ok=True)
        if not saved.exists():
            saved.write_bytes(old)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".engine-tmp")
    try:
        temporary.write_bytes(content)
        temporary.replace(path)
    except OSError:
        if temporary.exists():
            temporary.unlink()
        raise
    changes.append({"path": str(path), "backup": str(saved) if saved else None,
                    "sha256": hashlib.sha256(content).hexdigest()})


def png_bytes(image):
    stream = io.BytesIO()
    image.save(stream, format="PNG", compress_level=6)
    return stream.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--aircraft", type=Path)
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--package-only", action="store_true",
                        help="Install from source without writing the build volume")
    parser.add_argument("--backup", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    if args.package_only and (not args.aircraft or args.prepare_only):
        parser.error("--package-only requires --aircraft and cannot use --prepare-only")
    source = json.loads(args.source.read_text())
    manifest_path = args.build_dir / "manifests/wings-exports.json"
    manifest = json.loads(manifest_path.read_text())
    exports = [row for row in manifest["exports"] if row["name"] in ENGINE_NAMES]
    if {row["name"] for row in exports} != ENGINE_NAMES or len(exports) != 5:
        raise ValueError("Expected exactly the five engine export materials")
    materials = {row["name"]: row for row in source["materials"]}
    changes, evidence, staged = [], [], []
    def source_texture(index):
        texture = source["textures"][index]
        image_index = texture.get("extensions", {}).get("MSFT_texture_dds", {}).get("source", texture.get("source"))
        name = source["images"][image_index]["uri"].replace("\\", "/").split("/")[-1]
        path = args.source.parents[4] / "common/texture" / name
        if not path.is_file():
            raise FileNotFoundError(path)
        return path

    for entry in exports:
        material = materials[entry["materials"][0]]
        pbr = material.get("pbrMetallicRoughness", {})
        metallic, roughness = pbr.get("metallicFactor", 1), pbr.get("roughnessFactor", 1)
        comp = pbr.get("metallicRoughnessTexture")
        if comp:
            texture_path = source_texture(comp["index"])
            dds, _ = ktx2_to_dds(texture_path)
            with Image.open(io.BytesIO(dds)) as decoded:
                image = material_map(decoded, metallic, roughness)
            origin = str(texture_path)
        else:
            image = Image.new("RGBA", (4, 4),
                              (max(F0_FLOOR, round(metallic * 255)),
                               round((1 - roughness) * 255), 0, 255))
            origin = "glTF metallicFactor/roughnessFactor defaults; no authored COMP asset"
        filename = Path(entry.get("texture_map_material_gloss", entry["name"] + "_material_gloss.png")).name
        entry["texture_map_material_gloss"] = "../textures/wings/" + filename
        entry["normal_metalness"] = True
        entry["specular"] = 1.0
        normal_source = source_texture(material["normalTexture"]["index"])
        normal_dds, _ = ktx2_to_dds(normal_source)
        strength = NACELLE_NORMAL_STRENGTH if entry["name"] in {"a380x_eng_lh", "a380x_eng_rh"} else 1.0
        with Image.open(io.BytesIO(normal_dds)) as decoded:
            normal = normal_map(decoded, strength)
        normal_filename = entry["name"] + "_normal.png"
        entry["texture_map_normal"] = "../textures/wings/" + normal_filename
        normal_content = png_bytes(normal)
        if not args.package_only:
            staged.append((args.build_dir / "textures/wings" / normal_filename, normal_content))
        content = png_bytes(image)
        if not args.package_only:
            staged.append((args.build_dir / "textures/wings" / filename, content))
        evidence.append({"object": entry["name"], "source": origin,
                         "normal_source": str(normal_source), "normal_conversion": "DirectX G inversion; RG amplitude scaled about neutral 127.5",
                         "normal_strength": strength,
                         "size": list(image.size), "f0_range": image.getchannel("R").getextrema(),
                         "gloss_range": image.getchannel("G").getextrema()})
        if not args.prepare_only:
            destinations = [] if args.package_only else [(args.build_dir / "xplane-obj/wings-corrected", args.build_dir / "textures/wings")]
            if args.aircraft:
                destinations.append((args.aircraft / "objects/exterior/wings", args.aircraft / "textures/exterior/wings"))
                staged.append((args.aircraft / "textures/exterior/wings" / filename, content))
                staged.append((args.aircraft / "textures/exterior/wings" / normal_filename, normal_content))
            for objects, textures in destinations:
                path = objects / (entry["name"] + ".obj")
                original = path.read_text()
                mapped = Path(os.path.relpath(textures / filename, objects)).as_posix()
                normal_mapped = Path(os.path.relpath(textures / normal_filename, objects)).as_posix()
                albedo_mapped = Path(os.path.relpath(textures / Path(entry["texture"]).name, objects)).as_posix()
                staged.append((path, patch_object(original, mapped, normal_mapped, albedo_mapped).encode()))
    if not args.package_only:
        staged.append((manifest_path, (json.dumps(manifest, indent=2) + "\n").encode()))
    # Livery material overrides need the same dielectric floor; their unique
    # paint, metal masks and roughness remain intact. max() is idempotent.
    if args.aircraft and not args.prepare_only:
        for entry in exports:
            filename = Path(entry["texture_map_material_gloss"]).name
            for path in (args.aircraft / "liveries").glob("*/textures/exterior/wings/" + filename):
                with Image.open(path) as im:
                    r, g, b, a = im.convert("RGBA").split()
                    r = r.point([max(F0_FLOOR, v) for v in range(256)])
                    staged.append((path, png_bytes(Image.merge("RGBA", (r, g, b, a)))))
    for path, content in staged:
        save_bytes(path, content, args.backup, changes)
    report = {"materials": evidence, "changed_files": changes,
              "build_cache_updated": not args.package_only,
              "albedo_pixels_unchanged": True,
              "references": ["https://developer.x-plane.com/article/the-x-plane-12-material-model/",
                             "https://developer.x-plane.com/article/obj8-file-format-specification/"],
              "validation": "Source channels and OBJ setup checked; native simulator appearance requires reload."}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"materials": len(evidence), "changed_files": len(changes), "report": str(args.report)}))


if __name__ == "__main__":
    main()
