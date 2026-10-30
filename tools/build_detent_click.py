#!/usr/bin/env python3
"""Synthesize the thrust-lever magnetic detent click (native/avionics/sounds/thrust_detent_click.wav).

Original synthesis, no recorded or third-party audio: a short magnet "snap" (band-limited noise
transient), the metallic ring of the detent spring (three damped partials), a low body "thunk" of the
pedestal and a small seating bounce 9 ms later. Mono PCM16, 44.1 kHz, about 80 ms, peak -6 dBFS.
The output is deterministic (fixed seed); `--check` fails if the committed file differs.

    python3 tools/build_detent_click.py            # (re)write the WAV
    python3 tools/build_detent_click.py --check    # verify the committed WAV
"""
from __future__ import annotations

import argparse
import math
import random
import struct
import sys
from pathlib import Path

RATE = 44100
DURATION_S = 0.080
PEAK_DBFS = -6.0
OUTPUT = Path(__file__).resolve().parents[1] / "native/avionics/sounds/thrust_detent_click.wav"


def synthesize() -> list[int]:
    rng = random.Random(380)
    count = int(RATE * DURATION_S)
    noise = [rng.uniform(-1.0, 1.0) for _ in range(count)]
    # Two-pole high-pass of the noise (removes rumble so the snap is crisp).
    snap, a, y1, x1 = [], 0.82, 0.0, 0.0
    for x in noise:
        y = a * (y1 + x - x1)
        snap.append(y)
        y1, x1 = y, x
    samples = []
    partials = ((2350.0, 0.0060, 0.55), (3900.0, 0.0040, 0.35), (5600.0, 0.0025, 0.22))
    for n in range(count):
        t = n / RATE
        value = 0.0
        # Magnet snap: 0.6 ms decay transient, plus the seating bounce at 9 ms.
        value += 1.00 * snap[n] * math.exp(-t / 0.0006)
        if t >= 0.009:
            tb = t - 0.009
            value += 0.40 * snap[n] * math.exp(-tb / 0.0005)
            value += 0.18 * math.sin(2 * math.pi * 2350.0 * tb) * math.exp(-tb / 0.003)
        # Metallic ring of the detent.
        for frequency, tau, gain in partials:
            value += gain * math.sin(2 * math.pi * frequency * t + 0.3) * math.exp(-t / tau)
        # Pedestal body thunk with a slight pitch drop.
        frequency = 210.0 * (1.0 - 0.15 * min(1.0, t / 0.03))
        value += 0.30 * math.sin(2 * math.pi * frequency * t) * math.exp(-t / 0.012)
        # 0.2 ms fade-in and a 10 ms raised-cosine tail: no DC step at either end.
        value *= min(1.0, t / 0.0002)
        tail = DURATION_S - t
        if tail < 0.010:
            value *= 0.5 - 0.5 * math.cos(math.pi * tail / 0.010)
        samples.append(value)
    peak = max(abs(v) for v in samples) or 1.0
    scale = 32767.0 * 10 ** (PEAK_DBFS / 20.0) / peak
    return [int(round(v * scale)) for v in samples]


def wav_bytes(samples: list[int]) -> bytes:
    data = struct.pack(f"<{len(samples)}h", *samples)
    fmt = struct.pack("<HHIIHH", 1, 1, RATE, RATE * 2, 2, 16)
    return (b"RIFF" + struct.pack("<I", 4 + 8 + len(fmt) + 8 + len(data)) + b"WAVE"
            + b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(data)) + data)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="verify the committed WAV instead of writing it")
    parser.add_argument("--output", type=Path, default=OUTPUT)
    args = parser.parse_args()
    encoded = wav_bytes(synthesize())
    if args.check:
        if not args.output.is_file() or args.output.read_bytes() != encoded:
            print(f"{args.output} is stale: python3 tools/build_detent_click.py", file=sys.stderr)
            return 1
        print(f"{args.output.name}: up to date ({len(encoded)} bytes)")
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(encoded)
    print(f"wrote {args.output} ({len(encoded)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
