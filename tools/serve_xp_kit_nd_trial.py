#!/usr/bin/env python3
"""Read-only ND/OANS website preview. Does not connect to or install into X-Plane."""
import argparse
import json
import mimetypes
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs
from nd_airport_preview import load_airports, amdb

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'native/avionics/xp-kit-nd'
BUILD = ROOT / 'build/xp-kit-nd-test'


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urlparse(self.path)
        query = parse_qs(parsed.query)
        if parsed.path == '/airports':
            search = query.get('q',[''])[0].upper()
            data = [a['catalogue'] for key,a in self.server.airports.items()
                    if search in key or search in a['catalogue']['name'].upper()]
            return self.respond(json.dumps(data).encode(), 'application/json')
        if parsed.path.startswith('/airport/') or parsed.path.startswith('/geometry/'):
            code = parsed.path.rsplit('/',1)[1]
            if code not in self.server.airports:
                return self.send_error(404)
            airport = self.server.airports[code]
            data = (amdb(airport,query.get('projection',[''])[0]) if parsed.path.startswith('/airport/')
                    else {key:value for key,value in airport.items() if key != 'features'})
            return self.respond(json.dumps(data,allow_nan=False).encode(),'application/json')
        names = {'/':'trial.html','/trial.js':'trial.js','/trial.css':'trial.css','/nd.html':'nd.html','/bridge.js':'bridge.js'}
        filename = SOURCE/names[parsed.path] if parsed.path in names else None
        if filename is None:
            relative = parsed.path.lstrip('/')
            allowed = relative.startswith(('ND/','runtime/','Fonts/','Images/')) or relative in ('manifest.json','LICENSE')
            candidate = (BUILD/relative).resolve()
            if allowed and candidate.is_relative_to(BUILD.resolve()):
                filename = candidate
        if filename is None or not filename.is_file():
            return self.send_error(404)
        self.respond(filename.read_bytes(),mimetypes.guess_type(filename)[0] or 'application/octet-stream')

    def respond(self, data, mime):
        self.send_response(200)
        self.send_header('Content-Type',mime)
        self.send_header('Content-Length',str(len(data)))
        self.send_header('Cache-Control','no-store')
        self.send_header('Content-Security-Policy',"default-src 'self' data: blob:; script-src 'self' 'unsafe-inline' 'unsafe-eval'; style-src 'self' 'unsafe-inline'; connect-src 'self'")
        self.end_headers()
        try:
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def log_message(self, *_args):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8382)
    parser.add_argument('--refresh-airports', action='store_true', help='Re-read local scenery instead of the preview cache')
    args = parser.parse_args()
    cache = BUILD/'airports.json'
    if cache.exists() and not args.refresh_airports:
        airports = json.loads(cache.read_text())
    else:
        print('Reading local airport scenery; aircraft files remain unchanged',flush=True)
        airports = load_airports()
        cache.write_text(json.dumps(airports))
    server = ThreadingHTTPServer(('127.0.0.1',args.port),Handler)
    server.airports = airports
    print(f'ND + OANS review: http://127.0.0.1:{server.server_port}/',flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == '__main__':
    main()
