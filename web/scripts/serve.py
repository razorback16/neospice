"""Serve only the built site on loopback; supports testing a Pages base path."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlsplit

parser = argparse.ArgumentParser()
parser.add_argument('--port', type=int, default=48173)
parser.add_argument('--base', default='/')
parser.add_argument('--directory', default=str(Path(__file__).resolve().parents[1] / 'dist'))
args = parser.parse_args()
root = Path(args.directory).resolve()
if not (root / 'index.html').is_file():
    parser.error(f'{root} has no build. Run npm run build first.')
base = '/' + args.base.strip('/') + '/' if args.base.strip('/') else '/'

class Handler(SimpleHTTPRequestHandler):
    extensions_map = {**SimpleHTTPRequestHandler.extensions_map, '.wasm': 'application/wasm', '.mjs': 'text/javascript'}
    def do_GET(self):
        if self.path.split('?')[0] == base.rstrip('/') and base != '/':
            self.send_response(301)
            self.send_header('Location', base)
            self.end_headers()
            return
        if not urlsplit(self.path).path.startswith(base):
            self.send_error(404)
            return
        self.path = '/' + self.path[len(base):]
        relative = unquote(urlsplit(self.path).path).lstrip('/')
        target = (root / relative).resolve()
        if not target.is_relative_to(root) or any(p.startswith('.') for p in Path(relative).parts):
            self.send_error(404)
            return
        super().do_GET()
    def do_HEAD(self):
        if not urlsplit(self.path).path.startswith(base):
            self.send_error(404)
            return
        self.path = '/' + self.path[len(base):]
        super().do_HEAD()
    def list_directory(self, path):
        self.send_error(404)
        return None
    def end_headers(self):
        self.send_header('Cache-Control', 'no-cache')
        self.send_header('X-Content-Type-Options', 'nosniff')
        super().end_headers()

print(f'Circuit Lab: http://127.0.0.1:{args.port}{base}', flush=True)
ThreadingHTTPServer(('127.0.0.1', args.port), partial(Handler, directory=str(root))).serve_forever()
