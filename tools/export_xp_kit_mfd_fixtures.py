"""Export native page-test binary drawing frames for WebKit/Chromium renderer QA."""
import argparse
import ctypes as C
import json
from pathlib import Path
from xp_kit_transport import MfdFrame, DrawList

PALETTE = (0x000000, 0xffffff, 0x00ffff, 0x00ff00, 0xe68000, 0xffff00, 0xff94ff,
           0xc4c6cf, 0x868a9c, 0x272525, 0x0c66f7, 0xff0000, 0x4f5467, 0x222222,
           0x3c3c3c, 0xff9601, 0x717070, 0xd3d3d3, 0x808080, 0x74757b, 0x00abab, 0x4d4dff)


def export(data):
    assert len(data) == C.sizeof(MfdFrame)
    frame = MfdFrame.from_buffer_copy(data)
    dl = frame.list
    assert not dl.overflowed and dl.cmd_count <= 4096 and dl.clip_count <= 256
    text = data[MfdFrame.list.offset + DrawList.text.offset:][:dl.text_len]
    commands = []
    for c in dl.cmds[:dl.cmd_count]:
        assert c.clip < dl.clip_count and c.color < len(PALETTE)
        payload = None
        if c.kind == 6:
            assert c.first + c.count <= dl.text_len
            payload = text[c.first:c.first + c.count].decode()
        elif 3 <= c.kind <= 5:
            assert c.first + c.count <= dl.point_count
            payload = [value for p in dl.points[c.first:c.first + c.count] for value in (p.x, p.y)]
        r = dl.clips[c.clip]
        commands.append([c.kind, PALETTE[c.color], c.font, c.anchor, c.width, c.size, *c.a,
                         payload, [r.x, r.y, r.w, r.h], c.layer, c.closed])
    return {'sequence': frame.sequence, 'powered': bool(frame.powered), 'uri': frame.uri.decode(), 'commands': commands}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    rows = []
    for path in sorted(args.source.rglob('*.mfd')):
        if path.name.startswith('._'):
            continue
        name = path.parent.name + '-' + path.stem
        frame = export(path.read_bytes())
        destination = args.output / (name + '.json')
        destination.write_text(json.dumps(frame, separators=(',', ':')))
        rows.append({'file': destination.name, 'uri': frame['uri'], 'commands': len(frame['commands'])})
    assert rows, 'No native page-test frames found'
    (args.output / 'index.json').write_text(json.dumps(rows, indent=2))
    print(json.dumps({'exported': len(rows), 'unique_routes': sorted({r['uri'] for r in rows})}, indent=2))


if __name__ == '__main__':
    main()
