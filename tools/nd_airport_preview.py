"""Read-only X-Plane apt.dat geometry for the ND/OANS browser review.

This is NOT a licensed AMDB or a source of declared operational distances.
Coordinates and pavement outlines are real scenery data; runway lengths are
geometric. No buildings, taxi clearances or BTV stopping predictions are invented.

Record definitions: https://developer.x-plane.com/article/airport-data-apt-dat-12-00-file-format-specification/
"""
import hashlib
import math
from pathlib import Path

XP = Path('/Users/sava/Library/Application Support/Steam/steamapps/common/X-Plane 12')
R = 6371000
# In projected airport metres, rather than a fixed count that facets long curves
# and oversamples short ones. Even a malformed source span has a bounded cost.
CURVE_TOLERANCE_M = .20
CURVE_MAX_DEPTH = 10
VERTEX_EPSILON_M = .001
TAXI_CENTRELINE_CODES = frozenset((1, 7, 51, 57))


def project(lat, lon, origin):
    lat, lon, lat0, lon0 = map(math.radians, (lat, lon, *origin))
    delta = lon - lon0
    dot = min(1, max(-1, math.sin(lat0) * math.sin(lat) + math.cos(lat0) * math.cos(lat) * math.cos(delta)))
    c = math.acos(dot)
    k = c / math.sin(c) if c > 1e-10 else 1
    return [R * k * math.cos(lat) * math.sin(delta),
            R * k * (math.cos(lat0) * math.sin(lat) - math.sin(lat0) * math.cos(lat) * math.cos(delta))]


def unproject(x, y, origin):
    lat0, lon0 = map(math.radians, origin)
    rho = math.hypot(x, y)
    if rho < 1e-8:
        return [origin[1], origin[0]]
    c = rho / R
    lat = math.asin(math.cos(c) * math.sin(lat0) + y * math.sin(c) * math.cos(lat0) / rho)
    lon = lon0 + math.atan2(x * math.sin(c), rho * math.cos(lat0) * math.cos(c) - y * math.sin(lat0) * math.sin(c))
    return [math.degrees(lon), math.degrees(lat)]


def scenery_sources(root=XP):
    sources = []
    ini = root / 'Custom Scenery/scenery_packs.ini'
    if ini.exists():
        for line in ini.read_text().splitlines():
            if line.startswith('SCENERY_PACK '):
                pack = line[len('SCENERY_PACK '):].strip()
                source = (root / ('Global Scenery/Global Airports' if pack == '*GLOBAL_AIRPORTS*' else pack)
                          / 'Earth nav data/apt.dat')
                if source.is_file() and source not in sources:
                    sources.append(source)
    fallback = root / 'Global Scenery/Global Airports/Earth nav data/apt.dat'
    if fallback.is_file() and fallback not in sources:
        sources.append(fallback)
    return sources


def airport_blocks(path, wanted):
    block, identifiers = [], set()
    with path.open(errors='replace') as file:
        for line in file:
            fields = line.split()
            if not fields or fields[0].startswith('#'):
                continue
            if fields[0] in ('1', '16', '17', '99'):
                matched = wanted.intersection(identifiers)
                for code in matched:
                    yield code, block
                block = []
                identifiers = {fields[4]} if len(fields) > 4 else set()
            if fields[0] == '1302' and len(fields) > 2 and fields[1] in ('icao_code', 'local_code'):
                identifiers.add(fields[2])
            block.append(line.rstrip())
    for code in wanted.intersection(identifiers):
        yield code, block


def _distance_to_segment_squared(point, first, last):
    dx, dy = last[0] - first[0], last[1] - first[1]
    length_squared = dx * dx + dy * dy
    t = (max(0, min(1, ((point[0] - first[0]) * dx + (point[1] - first[1]) * dy) / length_squared))
         if length_squared else 0)
    return (point[0] - first[0] - t * dx) ** 2 + (point[1] - first[1] - t * dy) ** 2


def _curve_endpoints(first, control1, control2, last, tolerance_m, max_depth):
    """Yield adaptively flattened cubic endpoints in source order.

    A Bezier lies in its control hull. Requiring both handles to lie within the
    chord's tolerance capsule bounds the whole span's geometric chord error,
    including collinear handles that overshoot either endpoint. The depth cap
    bounds pathological scenery input to 2**max_depth segments per source span.
    """
    stack = [(first, control1, control2, last, 0)]
    tolerance_squared = tolerance_m * tolerance_m
    while stack:
        a, b, c, d, depth = stack.pop()
        if (depth >= max_depth or
                max(_distance_to_segment_squared(b, a, d), _distance_to_segment_squared(c, a, d)) <= tolerance_squared):
            yield d
            continue
        ab, bc, cd = ([(p[j] + q[j]) * .5 for j in (0, 1)] for p, q in ((a, b), (b, c), (c, d)))
        abc, bcd = ([(p[j] + q[j]) * .5 for j in (0, 1)] for p, q in ((ab, bc), (bc, cd)))
        middle = [(abc[j] + bcd[j]) * .5 for j in (0, 1)]
        stack.append((middle, bcd, cd, d, depth + 1))
        stack.append((a, ab, abc, middle, depth + 1))


def flatten(nodes, origin, closed, *, tolerance_m=CURVE_TOLERANCE_M, max_depth=CURVE_MAX_DEPTH):
    """Project apt.dat nodes, preserving endpoints, ring closure and split handles."""
    if not nodes:
        return []
    if tolerance_m <= 0 or not math.isfinite(tolerance_m) or not 0 <= max_depth <= CURVE_MAX_DEPTH:
        raise ValueError('Invalid airport curve tolerance or subdivision bound')
    result = []
    projected = [project(*node[:2], origin) for node in nodes]
    controls = [project(*node[2:], origin) if len(node) == 4 else point for node, point in zip(nodes, projected)]

    def append(point):
        if not result or math.dist(result[-1], point) >= VERTEX_EPSILON_M:
            result.append(point)

    limit = len(nodes) if closed else len(nodes) - 1
    for i in range(limit):
        next_i = (i + 1) % len(nodes)
        first, last = nodes[i], nodes[next_i]
        a, b = projected[i], projected[next_i]
        # WED encodes split handles as colocated nodes. A span with only
        # one curved endpoint is a zero-length connector, not a real loop.
        if math.dist(a,b) < VERTEX_EPSILON_M and (len(first) != 4 or len(last) != 4):
            continue
        append(a)
        if len(first) == 4 or len(last) == 4:
            c2 = [2 * b[j] - controls[next_i][j] for j in (0, 1)]
            for point in _curve_endpoints(a, controls[i], c2, b, tolerance_m, max_depth):
                append(point)
        else:
            append(b)
    if result and closed:
        if math.dist(result[-1], result[0]) < VERTEX_EPSILON_M:
            result[-1] = result[0]
        else:
            result.append(result[0])
    elif result:
        # Retain the exact source endpoint, including sub-millimetre duplicates.
        result[-1] = projected[-1]
    return result


def convert(code, lines, source):
    header = lines[0].split(maxsplit=5)
    metadata = {f[1]: ' '.join(f[2:]) for line in lines if (f := line.split()) and f[0] == '1302' and len(f) > 2}
    runways = [f for line in lines if (f := line.split()) and f[0] == '100']
    if not runways:
        raise ValueError(f'{code}: no paved-runway coordinates')
    origin = (float(metadata.get('datum_lat', runways[0][9])), float(metadata.get('datum_lon', runways[0][10])))
    catalogue = {'idarpt': code, 'iata': metadata.get('iata_code'), 'name': header[5] if len(header) > 5 else code,
                 'coordinates': {'lat': origin[0], 'lon': origin[1]}, 'elev': float(header[1]) * .3048}
    features, identifier = {}, 0

    def add(layer, kind, geometry, **properties):
        nonlocal identifier
        identifier += 1
        features.setdefault(layer, {'type': 'FeatureCollection', 'features': []})['features'].append({
            'type': 'Feature', 'properties': {'feattype': kind, 'id': identifier, **properties}, 'geometry': geometry})

    add('aerodromereferencepoint', 26, {'type': 'Point', 'coordinates': [0, 0]},
        ident=code, name=catalogue['name'], iata=catalogue['iata'] or '')
    runway_info = []
    for f in runways:
        a, b = project(float(f[9]), float(f[10]), origin), project(float(f[18]), float(f[19]), origin)
        length, width = math.dist(a, b), float(f[1])
        if length < 10:
            continue
        nx, ny = -(b[1]-a[1])/length*width/2, (b[0]-a[0])/length*width/2
        ring = [[a[0]+nx,a[1]+ny], [b[0]+nx,b[1]+ny], [b[0]-nx,b[1]-ny], [a[0]-nx,a[1]-ny]]
        heading = math.degrees(math.atan2(b[0]-a[0], b[1]-a[1])) % 360
        pair = f[8]+'.'+f[17]
        add('runwayelement', 0, {'type':'Polygon','coordinates':[ring+[ring[0]]]}, idrwy=pair)
        add('paintedcenterline', 4, {'type':'LineString','coordinates':[a,b]}, idrwy=pair,
            midpoint={'type':'Point','coordinates':[(a[0]+b[0])/2,(a[1]+b[1])/2]})
        for name, point, bearing, displacement in ((f[8], a, heading, float(f[11])), (f[17], b, (heading+180)%360, float(f[20]))):
            direction = 1 if name == f[8] else -1
            threshold = [point[i] + direction*(b[i]-a[i])/length*displacement for i in (0,1)]
            add('runwaythreshold', 2, {'type':'Point','coordinates':threshold}, idthr=name, lda=round(length-displacement),
                tora=round(length), brngtrue=bearing, brngmag=bearing)
        runway_info.append({'name': f[8]+'/'+f[17], 'length_m': round(length), 'width_m': width})
    kind, nodes, rings, styles, source_name = None, [], [], [], ''

    def flush():
        if kind == 110 and rings:
            add('taxiwayelement', 14, {'type':'Polygon','coordinates':rings.copy()}, source_name=source_name)

    stands = []
    for line in lines[1:]:
        f = line.split()
        if not f or f[0].startswith('#'):
            continue
        record = int(f[0])
        if record not in range(111,117):
            flush(); kind, nodes, rings, styles, source_name = None, [], [], [], ''
        if record in (110,120):
            kind = record
            source_name = ' '.join(f[4:] if record == 110 else f[1:])
        elif 111 <= record <= 116 and kind:
            curved = record in (112,114,116)
            nodes.append(tuple(float(v) for v in f[1:5 if curved else 3]))
            styles.append({int(v) for v in f[5 if curved else 3:]})
            if record in (113,114,115,116):
                closed = record in (113,114)
                if kind == 110 and closed:
                    points = flatten(nodes, origin, True)
                    if len(points) >= 4:
                        rings.append(points)
                elif kind == 120 and len(nodes) >= 2:
                    # Only known taxi-centreline paint codes, not roadway,
                    # apron-edge, hold-short, light-only or unknown styles.
                    # Do not turn every painted shape into a taxi route.
                    chain, chain_codes = [], frozenset()

                    def flush_chain():
                        if len(chain) >= 2:
                            add('taxiwayguidanceline', 16, {'type':'LineString','coordinates':chain.copy()},
                                source_kind='xplane_painted_centerline', source_line_codes=sorted(chain_codes), source_name=source_name)

                    for i in range(len(nodes) if closed else len(nodes)-1):
                        codes = styles[i].intersection(TAXI_CENTRELINE_CODES)
                        if codes:
                            if chain and codes != chain_codes:
                                flush_chain()
                                chain = []
                            segment = flatten([nodes[i],nodes[(i+1)%len(nodes)]],origin,False)
                            if segment:
                                chain.extend(segment[1:] if chain else segment)
                                chain_codes = codes
                        elif chain:
                            flush_chain()
                            chain = []
                    flush_chain()
                nodes, styles = [], []
                if not closed:
                    kind = None
        elif record == 1300 and len(f) >= 7:
            lat, lon, heading = map(float, f[1:4])
            name = ' '.join(f[6:])
            add('parkingstandlocation', 23, {'type':'Point','coordinates':project(lat,lon,origin)}, idstd=name)
            stands.append({'name':name,'lat':lat,'lon':lon,'heading':heading})
    flush()
    # Scenery ATC taxi-edge geometry is useful where painted centrelines are
    # absent or use unknown styles. It is background geometry, NOT a clearance
    # or an assertion that a taxiway is suitable for the A380's wingspan.
    taxi_nodes = {f[4]: project(float(f[1]),float(f[2]),origin)
                  for line in lines if (f := line.split()) and f[0] == '1201' and len(f) >= 5}
    for line in lines:
        f = line.split()
        if len(f) >= 5 and f[0] == '1202' and f[4].startswith('taxiway') and f[1] in taxi_nodes and f[2] in taxi_nodes:
            add('taxiwayguidanceline',16,{'type':'LineString','coordinates':[taxi_nodes[f[1]],taxi_nodes[f[2]]]},
                idlin=' '.join(f[5:]), source_kind='xplane_atc_taxi_edge')
    return {'catalogue': catalogue, 'origin': origin, 'features': features, 'runways': runway_info,
            'stands': stands, 'source': str(source), 'source_block_sha256': hashlib.sha256('\n'.join(lines).encode()).hexdigest(),
            'limitations': ['Scenery geometry, not operational AMDB', 'Runway distances are geometric, not declared distances',
                            'Magnetic variation is zero in the preview fixture', 'No buildings or BTV stopping predictions supplied',
                            'ATC taxi edges are background geometry, not clearances or wingspan approvals; curved ATC edges are drawn straight']}


def load_airports(wanted=('UAAA', 'EGLL'), root=XP):
    airports, pending = {}, set(wanted)
    for source in scenery_sources(root):
        for code, lines in airport_blocks(source, pending):
            airports[code] = convert(code, lines, source)
            pending.discard(code)
            if not pending:
                return airports
        if not pending:
            break
    if pending:
        raise FileNotFoundError('Airport geometry missing: ' + ', '.join(sorted(pending)))
    return airports


def amdb(airport, projection):
    if projection != 'EPSG:4326':
        return airport['features']
    import copy
    data = copy.deepcopy(airport['features'])
    def walk(coords):
        if isinstance(coords[0], (float,int)):
            return unproject(*coords,airport['origin'])
        return [walk(point) for point in coords]
    for collection in data.values():
        for feature in collection['features']:
            feature['geometry']['coordinates'] = walk(feature['geometry']['coordinates'])
            if 'midpoint' in feature['properties']:
                feature['properties']['midpoint']['coordinates'] = walk(feature['properties']['midpoint']['coordinates'])
    return data
