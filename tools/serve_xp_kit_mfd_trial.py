"""Interactive local MFD mockup. No simulator API, aircraft writes or installation."""
import argparse
import ctypes as C
import json
import math
import mimetypes
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import threading
import time
from urllib.parse import urlparse
from export_xp_kit_mfd_fixtures import export
from xp_kit_transport import MfdFrame

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'native/avionics/xp-kit-mfd-preview'
BUILD=ROOT/'build/xp-kit-mfd-preview'
XPLANE=Path('/Users/sava/Library/Application Support/Steam/steamapps/common/X-Plane 12')
FIELDS=('cost_index','cruise_fl','zfw_kg','block_kg','v1','vr','v2','flaps','packs','anti_ice')


class Engine:
    def __init__(self):
        self.lib=C.CDLL(str(BUILD/'libMfdPreview.dylib'))
        for name,args,result in (
            ('preview_init',[C.c_char_p],C.c_int),('preview_reset',[C.c_int],None),
            ('preview_tick',[C.c_double],None),
            ('preview_event',[C.c_int,C.c_int,C.c_float,C.c_float,C.c_int,C.c_char_p],None),
            ('preview_navigate',[C.c_int,C.c_char_p],None),('preview_power',[C.c_int,C.c_int],None),
            ('preview_frame',[C.c_int],C.c_void_p),('preview_menu',[C.c_int],C.c_uint),
            ('preview_focus',[C.c_int],C.c_int),('preview_value',[C.c_char_p],C.c_double)):
            fn=getattr(self.lib,name);fn.argtypes=args;fn.restype=result
        if not self.lib.preview_init(str(XPLANE).encode()):
            raise RuntimeError('Local navigation data could not be loaded')
        self.lock=threading.Lock();self.last=time.monotonic();self.revision=0

    def state(self):
        now=time.monotonic();self.lib.preview_tick(min(now-self.last,1));self.last=now
        frames=[]
        for side in (0,1):
            frame=export(C.string_at(self.lib.preview_frame(side),C.sizeof(MfdFrame)))
            frame['menu_open']=self.lib.preview_menu(side)
            frame['keyboard']=bool(self.lib.preview_focus(side))
            frames.append(frame)
        values={name:self.lib.preview_value(name.encode()) for name in FIELDS}
        self.revision+=1
        return {'revision':self.revision,'mode':'isolated_preview','frames':frames,
                'values':{name:value if math.isfinite(value) else None for name,value in values.items()}}

    def action(self,data):
        if not isinstance(data,dict):raise ValueError('Expected an object')
        side=data.get('side',0)
        if type(side) is not int or side not in (0,1):raise ValueError('Invalid display side')
        action=data.get('action')
        if action=='events':
            events=data.get('events',[])
            if not isinstance(events,list) or len(events)>64:raise ValueError('Too many input events')
            validated=[]
            for e in events:
                if not isinstance(e,dict):raise ValueError('Invalid input event')
                kind=e.get('type');x=e.get('x',0);y=e.get('y',0);amount=e.get('amount',0);key=e.get('key','')
                if type(kind) is not int or kind not in range(6):raise ValueError('Invalid event kind')
                if any(type(v) not in (int,float) or not math.isfinite(v) for v in (x,y,amount)):raise ValueError('Invalid coordinates')
                if type(key) is not str or len(key)>20 or not key.isascii():raise ValueError('Invalid key')
                validated.append((side,kind,max(0,min(767,x)),max(0,min(1023,y)),max(-5,min(5,int(amount))),key.encode()))
            for event in validated:self.lib.preview_event(*event)
        elif action=='navigate':
            uri=data.get('uri','')
            if not isinstance(uri,str) or len(uri)>=64 or not uri.isascii():raise ValueError('Invalid page URI')
            self.lib.preview_navigate(side,uri.encode())
        elif action=='power':self.lib.preview_power(side,bool(data.get('on')))
        elif action=='reset':self.lib.preview_reset(bool(data.get('example')))
        else:raise ValueError('Unknown preview action')
        self.lib.preview_tick(.01)
        return self.state()


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        route=urlparse(self.path).path
        if route=='/api/state':
            with self.server.engine.lock:data=self.server.engine.state()
            return self.respond(json.dumps(data,allow_nan=False).encode(),'application/json')
        files={'/':'trial.html','/trial.js':'trial.js','/trial.css':'trial.css'}
        file=SOURCE/files[route] if route in files else None
        if route.startswith('/view/'):
            relative=route.removeprefix('/view/')
            candidate=(BUILD/'view'/relative).resolve()
            if candidate.is_relative_to((BUILD/'view').resolve()) and not candidate.name.startswith('._'):
                file=candidate
        if file is None or not file.is_file():return self.send_error(404)
        return self.respond(file.read_bytes(),mimetypes.guess_type(file)[0] or 'application/octet-stream')

    def do_POST(self):
        if self.path!='/api/action':return self.send_error(404)
        if self.headers.get('Origin')!=f'http://127.0.0.1:{self.server.server_port}':return self.send_error(403,'Same-origin preview input only')
        try:
            length=int(self.headers.get('Content-Length','0'))
            if length<1 or length>16384:raise ValueError('Invalid body length')
            if self.headers.get_content_type()!='application/json':raise ValueError('JSON input required')
            data=json.loads(self.rfile.read(length))
            with self.server.engine.lock:result=self.server.engine.action(data)
            self.respond(json.dumps(result,allow_nan=False).encode(),'application/json')
        except (ValueError,TypeError,json.JSONDecodeError) as error:self.send_error(400,str(error))

    def respond(self,data,mime):
        self.send_response(200);self.send_header('Content-Type',mime);self.send_header('Content-Length',str(len(data)))
        self.send_header('Cache-Control','no-store');self.send_header('X-Content-Type-Options','nosniff')
        self.send_header('Content-Security-Policy',"default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; connect-src 'self'; frame-ancestors 'self'")
        self.end_headers()
        try:self.wfile.write(data)
        except (BrokenPipeError,ConnectionResetError):pass

    def log_message(self,*_args):pass


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--port',type=int,default=8383)
    args=parser.parse_args()
    server=ThreadingHTTPServer(('127.0.0.1',args.port),Handler);server.engine=Engine()
    print(f'Interactive MFD review: http://127.0.0.1:{server.server_port}/',flush=True)
    try:server.serve_forever()
    except KeyboardInterrupt:pass
    finally:server.server_close()


if __name__=='__main__':main()
