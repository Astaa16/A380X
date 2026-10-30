"""Small, strict OBJ8 triangle/animation evaluator for offline model previews.

Coordinates and animation evaluation follow Laminar's OBJ8 specification:
https://developer.x-plane.com/article/obj8-file-format-specification/
This is not a simulator or avionics renderer. Unknown datarefs default to zero;
the neutral ground pose sets native gear deployment and the gear handle to one.
"""
from __future__ import annotations

from array import array
from dataclasses import dataclass
from math import cos, radians, sin, sqrt
from pathlib import Path
from typing import Iterable, Mapping


IDENTITY = (1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.)


def multiply(a, b):
    return tuple(sum(a[row * 4 + k] * b[k * 4 + col] for k in range(4))
                 for row in range(4) for col in range(4))


def translation(value):
    matrix = list(IDENTITY)
    matrix[3], matrix[7], matrix[11] = value
    return tuple(matrix)


def rotation(axis, angle):
    size = sqrt(sum(v * v for v in axis))
    if size < 1e-12:
        raise ValueError('ANIM_rotate axis must not be zero')
    x, y, z = (v / size for v in axis)
    c, s = cos(radians(angle)), sin(radians(angle))
    t = 1 - c
    return (t*x*x+c, t*x*y-s*z, t*x*z+s*y, 0.,
            t*x*y+s*z, t*y*y+c, t*y*z-s*x, 0.,
            t*x*z-s*y, t*y*z+s*x, t*z*z+c, 0.,
            0., 0., 0., 1.)


def interpolate(keys, value):
    """OBJ8 extrapolates outside the key range; it does not clamp."""
    if not keys:
        raise ValueError('Animation has no keyframes')
    if any(keys[i][0] >= keys[i + 1][0] for i in range(len(keys) - 1)):
        raise ValueError('Animation keyframe values must be strictly increasing')
    if len(keys) == 1:
        return keys[0][1]
    pair = len(keys) - 2
    for i in range(len(keys) - 1):
        if value <= keys[i + 1][0]:
            pair = i
            break
    lo, hi = keys[pair], keys[pair + 1]
    fraction = (value - lo[0]) / (hi[0] - lo[0])
    return tuple(a + fraction * (b - a) for a, b in zip(lo[1], hi[1]))


@dataclass(frozen=True)
class DrawCall:
    start: int
    count: int
    matrix: tuple
    cull: bool
    alpha_cutoff: float | None
    transform_datarefs: tuple[str, ...] = ()
    source_line: int = 0
    draw_index: int = 0


@dataclass
class PreviewObject:
    positions: array
    uv: array
    indices: array
    draws: list[DrawCall]
    texture: str | None
    datarefs: dict[str, float]

    def bake(self, flip_v=False):
        """Apply each draw's transform, convert axes, reverse CW to CCW."""
        vertices, uv, faces, styles = [], [], [], []
        # One cache per distinct transform allows different TRIS calls to share
        # vertices, without constructing heavyweight matrix+index tuple keys.
        caches = {}
        for draw in self.draws:
            remap = caches.setdefault(draw.matrix, {})
            for start in range(draw.start, draw.start + draw.count, 3):
                face = []
                for source in (self.indices[start], self.indices[start + 2], self.indices[start + 1]):
                    if source not in remap:
                        position = self.positions[source * 3:source * 3 + 3]
                        matrix = draw.matrix
                        transformed = [sum(matrix[row * 4 + k] * position[k] for k in range(3)) + matrix[row * 4 + 3]
                                       for row in range(3)]
                        remap[source] = len(vertices)
                        vertices.append((transformed[0], -transformed[2], transformed[1]))
                        u, v = self.uv[source * 2:source * 2 + 2]
                        uv.append((u, 1 - v if flip_v else v))
                    face.append(remap[source])
                faces.append(tuple(face))
                styles.append((draw.cull, draw.alpha_cutoff))
        return vertices, uv, faces, styles


def parse_obj8(lines: Iterable[str], values: Mapping[str, float] | None = None,
               lod_distance: float = 0., source: str = '<OBJ8>') -> PreviewObject:
    positions, uv, indices = array('d'), array('d'), array('I')
    draws, refs = [], {}
    texture = None
    matrix, stack, hidden = IDENTITY, [], False
    transform_refs = ()
    draw_enabled, cull, cutoff, active_lod = True, True, None, True
    global_cutoff = None
    block, pending = None, None
    draw_index = 0
    values = values or {}

    def value_for(name):
        if name in values:
            value = float(values[name])
        elif name.startswith('sim/flightmodel2/gear/deploy_ratio[') or name == 'sim/cockpit2/controls/gear_handle_down':
            value = 1.
        else:
            value = 0.
        refs[name] = value
        return value

    def commit_pending():
        nonlocal matrix, pending, transform_refs
        if pending is None:
            return
        kind, keys, axis, name, loop = pending
        value = value_for(name) if name else 0.
        if loop is not None:
            value %= loop
        evaluated = interpolate(keys, value)
        transform = translation(evaluated) if kind == 'trans' else rotation(axis, evaluated[0])
        matrix = multiply(matrix, transform)
        if name and name not in transform_refs:
            transform_refs += (name,)
        pending = None

    for line_number, line in enumerate(lines, 1):
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        words = line.split()
        command, arguments = words[0], words[1:]
        try:
            if command == 'ANIM_keyframe_loop' and block is not None:
                # XPlane2Blender writes the loop as the last line of the key table.
                if len(arguments) != 1 or float(arguments[0]) <= 0:
                    raise ValueError('ANIM_keyframe_loop requires a positive period')
                block = (*block[:4], float(arguments[0]))
                continue
            if command == 'ANIM_keyframe_loop':
                if pending is None or len(arguments) != 1 or float(arguments[0]) <= 0:
                    raise ValueError('ANIM_keyframe_loop requires a preceding completed animation and positive period')
                pending = (*pending[:4], float(arguments[0]))
                continue
            commit_pending()
            if block and command not in (f'ANIM_{block[0]}_key', f'ANIM_{block[0]}_end'):
                raise ValueError(f'Unexpected command inside ANIM_{block[0]} key table')
            if command == 'TEXTURE':
                texture = line.split(maxsplit=1)[1] if arguments else None
            elif command == 'VT':
                if len(arguments) != 8:
                    raise ValueError('VT requires eight values')
                data = list(map(float, arguments))
                positions.extend(data[:3])
                uv.extend(data[6:8])
            elif command in ('IDX', 'IDX10'):
                if len(arguments) != (1 if command == 'IDX' else 10):
                    raise ValueError(f'{command} has incorrect index count')
                indices.extend(map(int, arguments))
            elif command == 'ANIM_begin':
                stack.append((matrix, hidden, transform_refs))
            elif command == 'ANIM_end':
                if not stack:
                    raise ValueError('Unmatched ANIM_end')
                matrix, hidden, transform_refs = stack.pop()
            elif command.startswith('ANIM_'):
                if not stack:
                    raise ValueError('Animation command outside ANIM_begin/end')
                if command in ('ANIM_hide', 'ANIM_show'):
                    lo, hi = map(float, arguments[:2])
                    if len(arguments) != 3 or hi < lo:
                        raise ValueError('Invalid animation visibility range')
                    if lo <= value_for(arguments[2]) <= hi:
                        hidden = command == 'ANIM_hide'
                elif command in ('ANIM_trans', 'ANIM_rotate'):
                    kind = command[5:]
                    base = 6 if kind == 'trans' else 5
                    if len(arguments) not in (base, base + 2, base + 3):
                        raise ValueError(f'Unsupported {command} argument count')
                    numbers = list(map(float, arguments[:base]))
                    axis = None if kind == 'trans' else tuple(numbers[:3])
                    first = tuple(numbers[:3]) if kind == 'trans' else (numbers[3],)
                    second = tuple(numbers[3:6]) if kind == 'trans' else (numbers[4],)
                    if len(arguments) == base:
                        if first != second:
                            raise ValueError('Constant animation must have matching endpoints')
                        keys, name = [(0., first)], None
                    else:
                        lo, hi = map(float, arguments[base:base + 2])
                        keys = [(lo, first), (hi, second)] if lo != hi else [(lo, first)]
                        if lo == hi and first != second:
                            raise ValueError('Different endpoints for zero animation range')
                        name = arguments[base + 2] if len(arguments) > base + 2 else None
                    pending = (kind, keys, axis, name, None)
                elif command in ('ANIM_trans_begin', 'ANIM_rotate_begin'):
                    kind = command[5:-6]
                    if len(arguments) != (1 if kind == 'trans' else 4):
                        raise ValueError('Invalid keyframe animation header')
                    axis = None if kind == 'trans' else tuple(map(float, arguments[:3]))
                    block = (kind, [], axis, arguments[-1], None)
                elif command in ('ANIM_trans_key', 'ANIM_rotate_key'):
                    if block is None or len(arguments) != (4 if command == 'ANIM_trans_key' else 2):
                        raise ValueError('Invalid animation key')
                    data = list(map(float, arguments))
                    block[1].append((data[0], tuple(data[1:])))
                elif command in ('ANIM_trans_end', 'ANIM_rotate_end'):
                    if block is None:
                        raise ValueError('Unmatched animation table end')
                    pending, block = block, None
                else:
                    raise ValueError(f'Unsupported animation command: {command}')
            elif command == 'TRIS':
                start, count = map(int, arguments)
                if start < 0 or count < 0 or count % 3 or start + count > len(indices):
                    raise ValueError('TRIS range outside index table or not triangles')
                if draw_enabled and not hidden and active_lod and count:
                    draws.append(DrawCall(start, count, matrix, cull, cutoff, transform_refs,
                                          line_number, draw_index))
                draw_index += 1
            elif command in ('ATTR_no_draw', 'ATTR_draw_disable'):
                draw_enabled = False
            elif command in ('ATTR_draw', 'ATTR_draw_enable'):
                draw_enabled = True
            elif command in ('ATTR_cull', 'ATTR_no_cull'):
                cull = command == 'ATTR_cull'
            elif command in ('ATTR_no_blend', 'GLOBAL_no_blend'):
                cutoff = float(arguments[0]) if arguments else .5
                if command == 'GLOBAL_no_blend':
                    global_cutoff = cutoff
            elif command == 'ATTR_blend':
                cutoff = None
            elif command == 'ATTR_LOD':
                if stack:
                    raise ValueError('LOD boundary inside animation block')
                lo, hi = map(float, arguments)
                active_lod = lo <= lod_distance < hi
                matrix, hidden, draw_enabled, cull, cutoff = IDENTITY, False, True, True, global_cutoff
                transform_refs = ()
            elif command in ('IF', 'ELSE', 'ENDIF'):
                raise ValueError('Conditional OBJ drawing is not supported by this preview')
        except (ValueError, IndexError, OverflowError) as error:
            raise ValueError(f'{source}:{line_number}: {error}') from error
    commit_pending()
    if stack or block:
        raise ValueError(f'{source}: Unclosed animation block')
    if indices and max(indices) >= len(positions) // 3:
        raise ValueError(f'{source}: Index outside VT table')
    return PreviewObject(positions, uv, indices, draws, texture, refs)


def load_obj8(path: Path, **kwargs) -> PreviewObject:
    with Path(path).open() as stream:
        return parse_obj8(stream, source=str(path), **kwargs)
