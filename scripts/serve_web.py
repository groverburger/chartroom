#!/usr/bin/env python3
"""Local web preview server: static build plus a fixed-origin Yahoo route."""
import argparse
import http.server
import pathlib
import re
import urllib.error
import urllib.parse
import urllib.request

class Handler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if not self.path.startswith('/yahoo/'):
            return super().do_GET()
        parsed = urllib.parse.urlsplit(self.path)
        symbol = urllib.parse.unquote(parsed.path.removeprefix('/yahoo/v8/finance/chart/'))
        if not parsed.path.startswith('/yahoo/v8/finance/chart/') or not re.fullmatch(r'[A-Z0-9.^=_-]{1,32}', symbol):
            return self.send_error(400, 'Invalid symbol')
        query = urllib.parse.parse_qs(parsed.query)
        if query.get('interval', [''])[0] not in ('1m', '1h', '1d', '1wk'):
            return self.send_error(400, 'Invalid interval')
        clean = {k: v[0] for k, v in query.items() if k in ('interval', 'period1', 'period2')}
        if any(not clean.get(k, '').isdigit() for k in ('period1', 'period2')):
            return self.send_error(400, 'Invalid period')
        clean['includePrePost'] = 'false'
        url = 'https://query1.finance.yahoo.com/v8/finance/chart/' + urllib.parse.quote(symbol, safe='') + '?' + urllib.parse.urlencode(clean)
        try:
            request = urllib.request.Request(url, headers={'User-Agent': 'Chartroom/0.1', 'Accept': 'application/json'})
            with urllib.request.urlopen(request, timeout=25) as response:
                body = response.read(64 * 1024 * 1024)
                status = response.status
        except urllib.error.HTTPError as error:
            status, body = error.code, error.read()
        except (OSError, urllib.error.URLError) as error:
            return self.send_error(502, str(error))
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(body)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', default='build-web')
    parser.add_argument('--port', type=int, default=8080)
    args = parser.parse_args()
    root = pathlib.Path(args.directory).resolve()
    if not (root / 'chartroom.html').exists():
        parser.error('Build the WebAssembly target first.')
    factory = lambda *a, **kw: Handler(*a, directory=str(root), **kw)
    print(f'Chartroom: http://localhost:{args.port}/chartroom.html', flush=True)
    http.server.ThreadingHTTPServer(('127.0.0.1', args.port), factory).serve_forever()
