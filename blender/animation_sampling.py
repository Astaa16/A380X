"""Pure sampling plan shared by animation conversion and regression tests."""
from __future__ import annotations

import math


def sampling_plan(frames, mapping):
    """Return source samples and their paired dataref values.

    OBJ8 extrapolates past its outer keys. Optional constant endpoint guards
    make that extrapolation flat, including for reversed dataref intervals.
    Separate integer export frames retain duplicate source endpoint poses.

    ``sample_points`` supports mechanisms whose pose is not monotonic in the
    driving dataref, such as landing-gear doors that are closed at both ends
    of travel and open in between. Each point supplies a normalized source
    fraction and a dataref value; point order becomes export-frame order.
    """
    frames = sorted(set(float(frame) for frame in frames))
    points = mapping.get("sample_points")
    if points is not None:
        if not frames or not all(math.isfinite(value) for value in frames):
            raise ValueError("Animation sampling requires finite frames and values")
        if not isinstance(points, list) or len(points) < 2:
            raise ValueError("Explicit animation sampling requires at least two points")
        fractions = [float(point["source_fraction"]) for point in points]
        values = [float(point["dataref_value"]) for point in points]
        if not all(math.isfinite(value) for value in fractions + values):
            raise ValueError("Explicit animation samples must be finite")
        if not all(0 <= fraction <= 1 for fraction in fractions):
            raise ValueError("Explicit source fractions must be between zero and one")
        differences = [right - left for left, right in zip(values, values[1:])]
        if not differences or not (all(delta > 0 for delta in differences)
                                   or all(delta < 0 for delta in differences)):
            raise ValueError("Explicit dataref sample values must be strictly monotonic")
        start, span = frames[0], frames[-1] - frames[0]
        return [start + span * fraction for fraction in fractions], values

    first, last = float(mapping.get("source_start", 0)), float(mapping.get("source_end", 1))
    low, high = float(mapping["value_min"]), float(mapping["value_max"])
    if not frames or not all(math.isfinite(value) for value in frames + [first, last, low, high]):
        raise ValueError("Animation sampling requires finite frames and values")
    if not 0 <= first < last <= 1 or low == high:
        raise ValueError("Animation sampling requires nonzero source and dataref intervals")
    start = frames[0] + (frames[-1] - frames[0]) * first
    end = frames[0] + (frames[-1] - frames[0]) * last
    if end == start:
        # A single authored pose is constant over the requested input interval.
        samples, values = [start, end], [low, high]
    else:
        samples = sorted({start, end, *(frame for frame in frames if start < frame < end)})
        values = [low + (frame - start) / (end - start) * (high - low) for frame in samples]
    if mapping.get("clamp", False):
        span = high - low
        samples = [start, *samples, end]
        values = [low - span, *values, high + span]
    return samples, values
