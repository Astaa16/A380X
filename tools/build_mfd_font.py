#!/usr/bin/env python3
"""Bake the pinned FBW A380X MFD fonts into per-size alpha atlases for the native MFD.

Spec: docs/mfd-spec/01-framework-widgets.md section 2 (fonts) and 12-architecture.md section 2.

* Family SZ  = FBW-Display-EIS-A380-SlashedZero.ttf (default MFD font).
* Family Ecam = FBW-Display-EIS-A380.ttf, which differs from SZ only in the glyphs '0' and 'W';
  those two glyphs are stored as extra slots, everything else is shared.
* Glyphs: ASCII 32-126, U+00B0, U+25AF, U+25A1, U+25C7 (+ Ecam '0' and 'W').
* One atlas strip per pixel size actually used by the MFD, rasterised at that size with
  FreeType (via Pillow) so text is crisp without resampling. Advances are not stored per glyph:
  both fonts are monospaced with advance 2568/4096 em = 0.626953125 x size.
* The packed alpha atlas is compressed with a small token RLE (see mfd_font_decode in the output).

Usage: tools/build_mfd_font.py [--source-dir DIR] [--output FILE]
"""
from __future__ import annotations

import argparse
import base64
import hashlib
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve()
PORT = HERE.parents[1]
DEFAULT_SOURCE = (HERE.parents[2] / 'upstream-aircraft' / 'fbw-a380x' / 'src' / 'base' /
                  'flybywire-aircraft-a380-842' / 'html_ui' / 'Fonts' / 'fbw-a380x')
DEFAULT_OUTPUT = PORT / 'native' / 'avionics' / 'a380x_mfd_font.h'
SZ_NAME = 'FBW-Display-EIS-A380-SlashedZero.ttf'
ECAM_NAME = 'FBW-Display-EIS-A380.ttf'

# Pixel sizes used by the MFD (spec 01 section 4 plus the ATCCOM 35 px headings).
SIZES = [20, 21, 22, 24, 25, 26, 27, 28, 30, 35]
EXTRA = [0x00B0, 0x25AF, 0x25A1, 0x25C7]
ATLAS_W = 1024
PAD = 1


def slots():
    """(codepoint, use_ecam_font) per slot, in slot order."""
    out = [(c, False) for c in range(32, 127)]
    out += [(c, False) for c in EXTRA]
    out += [(ord('0'), True), (ord('W'), True)]
    return out


def rasterise(font: ImageFont.FreeTypeFont, ch: str):
    left, top, right, bottom = font.getbbox(ch, anchor='ls')
    w, h = right - left, bottom - top
    if w <= 0 or h <= 0:
        return None
    img = Image.new('L', (w, h), 0)
    ImageDraw.Draw(img).text((-left, -top), ch, font=font, fill=255, anchor='ls')
    # Trim fully transparent borders that FreeType's bbox may include.
    bbox = img.getbbox()
    if bbox is None:
        return None
    img = img.crop(bbox)
    return img, left + bbox[0], top + bbox[1]


def encode(raw: bytes) -> list[int]:
    """Token RLE: 0x00-0x3F = 1..64 zeros, 0x40-0x7F = 1..64 x 255, 0x80-0xFF = literal (7-bit alpha)."""
    out: list[int] = []
    i, n = 0, len(raw)
    while i < n:
        v = raw[i]
        if v == 0 or v == 255:
            j = i
            while j < n and raw[j] == v and j - i < 64:
                j += 1
            out.append((0x00 if v == 0 else 0x40) | (j - i - 1))
            i = j
        else:
            out.append(0x80 | (v >> 1))
            i += 1
    return out


def decode(tokens: list[int], size: int) -> bytes:
    out = bytearray()
    for t in tokens:
        if t < 0x40:
            out += bytes(t + 1)
        elif t < 0x80:
            out += b'\xff' * (t - 0x40 + 1)
        else:
            q = t & 0x7F
            out.append((q * 255 + 63) // 127)
    assert len(out) == size, (len(out), size)
    return bytes(out)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--source-dir', type=Path, default=DEFAULT_SOURCE)
    parser.add_argument('--output', type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument('--preview', type=Path, help='optional PNG dump of the atlas')
    args = parser.parse_args()
    sz_path, ecam_path = args.source_dir / SZ_NAME, args.source_dir / ECAM_NAME
    sz_hash = hashlib.sha256(sz_path.read_bytes()).hexdigest()
    ecam_hash = hashlib.sha256(ecam_path.read_bytes()).hexdigest()

    slot_list = slots()
    # Rasterise every (size, slot).
    bitmaps = []  # (size_index, slot, img or None, left, top)
    for si, size in enumerate(SIZES):
        f_sz = ImageFont.truetype(str(sz_path), size)
        f_ec = ImageFont.truetype(str(ecam_path), size)
        for slot, (cp, ecam) in enumerate(slot_list):
            r = rasterise(f_ec if ecam else f_sz, chr(cp))
            if r is None:
                bitmaps.append((si, slot, None, 0, 0))
            else:
                bitmaps.append((si, slot, r[0], r[1], r[2]))

    # Shelf-pack by descending height.
    order = sorted([b for b in bitmaps if b[2] is not None], key=lambda b: (-b[2].height, -b[2].width))
    x = y = shelf_h = 0
    place = {}
    for b in order:
        img = b[2]
        w, h = img.width + PAD, img.height + PAD
        if x + w > ATLAS_W:
            x, y, shelf_h = 0, y + shelf_h, 0
        place[(b[0], b[1])] = (x, y)
        x += w
        shelf_h = max(shelf_h, h)
    atlas_h = y + shelf_h
    atlas_h = (atlas_h + 3) & ~3
    atlas = Image.new('L', (ATLAS_W, atlas_h), 0)
    glyph_rows = []
    for si, slot, img, left, top in bitmaps:
        if img is None:
            glyph_rows.append((0, 0, 0, 0, 0, 0))
            continue
        gx, gy = place[(si, slot)]
        atlas.paste(img, (gx, gy))
        if not (-128 <= left <= 127 and -128 <= top <= 127 and img.width < 256 and img.height < 256):
            raise ValueError('glyph metrics out of range')
        glyph_rows.append((gx, gy, img.width, img.height, left, top))
    raw = atlas.tobytes()
    tokens = encode(raw)
    decoded = decode(tokens, len(raw))
    max_err = max(abs(a - b) for a, b in zip(raw, decoded))
    if max_err > 2:
        raise ValueError('RLE round trip error too large')
    if args.preview:
        Image.frombytes('L', atlas.size, decoded).save(args.preview)

    n_slots = len(slot_list)
    lines = [
        '/* SPDX-License-Identifier: GPL-3.0-or-later',
        ' *',
        ' * GENERATED by tools/build_mfd_font.py - do not edit.',
        ' * MFD glyph atlases (one strip per pixel size) for the native A380X MFD renderer.',
        f' * Font SHA-256 ({SZ_NAME}): {sz_hash}',
        f' * Font SHA-256 ({ECAM_NAME}, Ecam 0/W slots): {ecam_hash}',
        ' * Source: FlyByWire A380X html_ui/Fonts/fbw-a380x (GPL-3.0).',
        ' *',
        ' * Glyph metrics are in pixels at the strip size: (left, top) is the ink box offset from the',
        ' * pen position on the baseline (top < 0 = above the baseline). The advance is identical for',
        ' * every glyph: MFD_FONT_ADVANCE_EM x size. Decode the atlas once with mfd_font_decode().',
        ' */',
        '#ifndef A380X_MFD_FONT_H',
        '#define A380X_MFD_FONT_H',
        '',
        f'#define MFD_FONT_ATLAS_W {ATLAS_W}',
        f'#define MFD_FONT_ATLAS_H {atlas_h}',
        f'#define MFD_FONT_SIZE_COUNT {len(SIZES)}',
        f'#define MFD_FONT_SLOTS {n_slots}',
        '#define MFD_FONT_ADVANCE_EM 0.626953125f',
        '#define MFD_FONT_SLOT_DEGREE 95',
        '#define MFD_FONT_SLOT_BOX_V 96      /* U+25AF white vertical rectangle */',
        '#define MFD_FONT_SLOT_SQUARE 97     /* U+25A1 white square */',
        '#define MFD_FONT_SLOT_DIAMOND 98    /* U+25C7 white diamond */',
        '#define MFD_FONT_SLOT_ECAM_ZERO 99',
        '#define MFD_FONT_SLOT_ECAM_W 100',
        f'#define MFD_FONT_RLE_BYTES {len(tokens)}',
        '',
        'typedef struct { unsigned short x, y; unsigned char w, h; signed char left, top; } MfdFontGlyph;',
        '',
        'static const unsigned char mfd_font_sizes[MFD_FONT_SIZE_COUNT] = {' +
        ', '.join(str(s) for s in SIZES) + '};',
        '',
        '/* [size index * MFD_FONT_SLOTS + slot]; w == 0 means an empty glyph (space, lowercase, ...). */',
        'static const MfdFontGlyph mfd_font_glyphs[MFD_FONT_SIZE_COUNT * MFD_FONT_SLOTS] = {',
    ]
    for si, size in enumerate(SIZES):
        lines.append(f'    /* {size} px */')
        row = glyph_rows[si * n_slots:(si + 1) * n_slots]
        for k in range(0, n_slots, 6):
            lines.append('    ' + ' '.join('{%d,%d,%d,%d,%d,%d},' % g for g in row[k:k + 6]))
    b64 = base64.b64encode(bytes(tokens)).decode('ascii')
    lines += ['};', '',
              '/* Token RLE (0x00-0x3F = 1..64 zeros, 0x40-0x7F = 1..64 x 255, 0x80-0xFF = 7-bit alpha),',
              ' * stored base64-encoded to keep this header small. */',
              'static const char mfd_font_rle_b64[] =']
    for k in range(0, len(b64), 96):
        lines.append('    "' + b64[k:k + 96] + '"')
    lines[-1] += ';'
    lines += [
        '',
        'static inline int mfd_font_b64_value(char c)',
        '{',
        "    if (c >= 'A' && c <= 'Z') return c - 'A';",
        "    if (c >= 'a' && c <= 'z') return c - 'a' + 26;",
        "    if (c >= '0' && c <= '9') return c - '0' + 52;",
        "    if (c == '+') return 62;",
        "    if (c == '/') return 63;",
        '    return -1;',
        '}',
        '',
        '/* Atlas slot for a code point; -1 if the MFD font has no glyph for it (draw nothing). */',
        'static inline int mfd_font_slot(unsigned int cp, int ecam)',
        '{',
        "    if (ecam && cp == '0') return MFD_FONT_SLOT_ECAM_ZERO;",
        "    if (ecam && cp == 'W') return MFD_FONT_SLOT_ECAM_W;",
        '    if (cp >= 32 && cp <= 126) return (int)cp - 32;',
        '    if (cp == 0xB0u) return MFD_FONT_SLOT_DEGREE;',
        '    if (cp == 0x25AFu) return MFD_FONT_SLOT_BOX_V;',
        '    if (cp == 0x25A1u) return MFD_FONT_SLOT_SQUARE;',
        '    if (cp == 0x25C7u) return MFD_FONT_SLOT_DIAMOND;',
        '    if (cp == 0xA0u) return 0; /* NBSP draws as a space */',
        '    return -1;',
        '}',
        '',
        '/* Strip to use for a pixel size: exact match, else the nearest larger strip (scaled down),',
        ' * else the largest. *scale receives the factor to apply to the strip metrics. */',
        'static inline int mfd_font_size_index(float px, float *scale)',
        '{',
        '    int i, best = MFD_FONT_SIZE_COUNT - 1;',
        '    for (i = 0; i < MFD_FONT_SIZE_COUNT; ++i) {',
        '        if ((float)mfd_font_sizes[i] >= px - 0.01f) { best = i; break; }',
        '    }',
        '    if (scale) *scale = px / (float)mfd_font_sizes[best];',
        '    return best;',
        '}',
        '',
        '/* Decode the atlas into dst (MFD_FONT_ATLAS_W * MFD_FONT_ATLAS_H bytes). Returns 1 on success. */',
        'static inline int mfd_font_decode(unsigned char *dst)',
        '{',
        '    unsigned long o = 0, n = (unsigned long)MFD_FONT_ATLAS_W * MFD_FONT_ATLAS_H;',
        '    unsigned long i, k, produced = 0;',
        '    unsigned int acc = 0, bits = 0;',
        '    for (i = 0; mfd_font_rle_b64[i] && produced < (unsigned long)MFD_FONT_RLE_BYTES; ++i) {',
        '        unsigned char t;',
        '        int v = mfd_font_b64_value(mfd_font_rle_b64[i]);',
        '        if (v < 0) continue; /* padding */',
        '        acc = (acc << 6) | (unsigned int)v; bits += 6;',
        '        if (bits < 8) continue;',
        '        bits -= 8; t = (unsigned char)((acc >> bits) & 0xFFu); ++produced;',
        '        if (t < 0x80u) {',
        '            unsigned long run = (unsigned long)(t & 0x3Fu) + 1u;',
        '            unsigned char v = (t < 0x40u) ? 0u : 255u;',
        '            if (o + run > n) return 0;',
        '            for (k = 0; k < run; ++k) dst[o++] = v;',
        '        } else {',
        '            if (o >= n) return 0;',
        '            dst[o++] = (unsigned char)(((unsigned)(t & 0x7Fu) * 255u + 63u) / 127u);',
        '        }',
        '    }',
        '    return o == n && produced == (unsigned long)MFD_FONT_RLE_BYTES;',
        '}',
        '',
        '#endif',
        '',
    ]
    args.output.write_text('\n'.join(lines))
    print(f'{len(SIZES)} sizes x {n_slots} slots, atlas {ATLAS_W}x{atlas_h}, '
          f'{len(tokens)} RLE bytes, header {args.output.stat().st_size} bytes')


if __name__ == '__main__':
    main()
