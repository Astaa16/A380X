#!/usr/bin/env python3
"""One-shot scenery reader, launched off the simulator thread by the local helper."""
import json
import re
import sys
from pathlib import Path
from nd_airport_preview import load_airports, amdb

if len(sys.argv) != 3 or not re.fullmatch(r'[A-Z0-9_-]{2,16}',sys.argv[2]):
    raise SystemExit('Expected X-Plane directory and airport identifier')
airport = load_airports((sys.argv[2],),Path(sys.argv[1]))[sys.argv[2]]
print(json.dumps({'catalogue':airport['catalogue'],'metric':amdb(airport,''),
                  'wgs84':amdb(airport,'EPSG:4326'),'source':airport['source'],
                  'source_block_sha256':airport['source_block_sha256'],'limitations':airport['limitations']},allow_nan=False))
