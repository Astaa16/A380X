#!/usr/bin/env python3
"""Serve only built, isolated ECAM review files on loopback. No simulator API."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


class Handler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; connect-src 'none'; img-src 'self' data:; font-src 'self'; frame-ancestors 'none'")
        super().end_headers()

    def list_directory(self, _path):
        self.send_error(403, 'Directory listing disabled')
        return None

    def log_message(self, *_args):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8384)
    parser.add_argument('--directory', type=Path, default=Path(__file__).resolve().parents[1] / 'build/ecam-preview')
    args = parser.parse_args()
    if not (args.directory / 'index.html').is_file() or not (args.directory / 'app.js').is_file():
        parser.error('Build the ECAM review first with tools/build_ecam_preview.cjs')
    server = ThreadingHTTPServer(('127.0.0.1', args.port), partial(Handler, directory=str(args.directory.resolve())))
    print(f'A380X ECAM review: http://127.0.0.1:{server.server_port}/', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == '__main__':
    main()
