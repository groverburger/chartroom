#!/usr/bin/env python3
"""Local Chartroom preview with fixed, validated public market-data routes."""
import argparse
import datetime
import http.server
import json
import pathlib
import re
import urllib.error
import urllib.parse
import urllib.request

UA = ('Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 '
      '(KHTML, like Gecko) Chrome/126.0 Safari/537.36')


def upstream(path, method='GET', body=None):
    """No user-controlled host, redirects, arbitrary paths or credentials."""
    parsed = urllib.parse.urlsplit(path)
    query = urllib.parse.parse_qs(parsed.query, keep_blank_values=True)
    if any(len(v) != 1 for v in query.values()):
        raise ValueError('Duplicate parameter')
    query = {k: v[0] for k, v in query.items()}
    headers = {'User-Agent': UA, 'Accept': 'application/json'}
    route = urllib.parse.unquote(parsed.path)
    if method == 'POST':
        if route != '/scanner/america/scan' or query:
            raise ValueError('Unsupported POST route')
        data = json.loads(body)
        allowed_columns = ['description', 'close', 'change', 'market_cap_basic', 'sector', 'volume', 'exchange']
        if data.get('columns') != allowed_columns:
            raise ValueError('Unsupported columns')
        bounds = data.get('range', [])
        if len(bounds) != 2 or any(type(v) is not int for v in bounds) or not 0 <= bounds[0] < bounds[1] <= 100100 or bounds[1] - bounds[0] > 100:
            raise ValueError('Invalid range')
        sort = data.get('sort', {})
        if sort.get('sortBy') not in ('market_cap_basic', 'change', 'volume') or sort.get('sortOrder') not in ('asc', 'desc'):
            raise ValueError('Invalid sort')
        filters = data.get('filter', [])
        if not isinstance(filters, list) or len(filters) > 10:
            raise ValueError('Invalid filters')
        for f in filters:
            if f.get('left') not in ('type', 'typespecs', 'exchange', 'close', 'market_cap_basic', 'volume', 'sector') or f.get('operation') not in ('equal', 'has', 'in_range', 'egreater'):
                raise ValueError('Invalid filter')
        headers.update({'Content-Type': 'application/json', 'Origin': 'https://www.tradingview.com', 'Referer': 'https://www.tradingview.com/'})
        return 'https://scanner.tradingview.com/america/scan', headers, json.dumps({k: data[k] for k in ('columns', 'range', 'sort', 'filter')}).encode()
    if method != 'GET':
        raise ValueError('Unsupported method')
    if route.startswith('/yahoo/v8/finance/chart/'):
        headers['User-Agent'] = 'Chartroom/0.1'
        symbol = route.removeprefix('/yahoo/v8/finance/chart/')
        if not re.fullmatch(r'[A-Z0-9.^=_-]{1,32}', symbol):
            raise ValueError('Invalid symbol')
        if query.get('interval') not in ('1m', '1h', '1d', '1wk'):
            raise ValueError('Invalid interval')
        if any(not query.get(k, '').isdigit() for k in ('period1', 'period2')):
            raise ValueError('Invalid period')
        clean = {k: query[k] for k in ('interval', 'period1', 'period2')}
        extended = query.get('includePrePost', 'false')
        if extended not in ('true', 'false'):
            raise ValueError('Invalid extended-hours flag')
        clean['includePrePost'] = extended if query['interval'] == '1m' else 'false'
        return 'https://query1.finance.yahoo.com/v8/finance/chart/' + urllib.parse.quote(symbol, safe='') + '?' + urllib.parse.urlencode(clean), headers, None
    if route == '/yahoo/v1/finance/search':
        # Ticker name lookups for watchlist tooltips: one exact symbol, fixed result sizes.
        headers['User-Agent'] = 'Chartroom/0.1'
        fixed = {'quotesCount': '5', 'newsCount': '0', 'listsCount': '0'}
        if set(query) != {'q', *fixed} or any(query[k] != v for k, v in fixed.items()):
            raise ValueError('Invalid search')
        if not re.fullmatch(r'[A-Z0-9.^=_-]{1,32}', query['q']):
            raise ValueError('Invalid symbol')
        return 'https://query1.finance.yahoo.com/v1/finance/search?' + urllib.parse.urlencode({'q': query['q'], **fixed}), headers, None
    company = re.fullmatch(r'/nasdaq/api/(company/([A-Z-]{1,10})/(company-profile|earnings-surprise)|analyst/([A-Z-]{1,10})/earnings-date)', route)
    if company:
        if query:
            raise ValueError('Unexpected fundamentals parameter')
        headers.update({'Origin': 'https://www.nasdaq.com', 'Referer': 'https://www.nasdaq.com/'})
        return 'https://api.nasdaq.com' + route.removeprefix('/nasdaq'), headers, None
    match = re.fullmatch(r'/nasdaq/api/quote/([A-Z-]{1,10})/(info|chart|option-chain|summary)', route)
    if match:
        if query.get('assetclass') not in ('stocks', 'etf'):
            raise ValueError('Invalid asset class')
        allowed = {'assetclass', 'fromdate', 'todate', 'limit', 'offset', 'money', 'type', 'excode', 'callput'}
        if set(query) - allowed:
            raise ValueError('Unknown parameter')
        for k in ('fromdate', 'todate'):
            if k in query and not (k == 'fromdate' and query[k] == 'all' and match[2] == 'option-chain'):
                datetime.date.fromisoformat(query[k])
        for k, maximum in (('limit', 2000), ('offset', 20000)):
            if k in query and (not query[k].isdigit() or not 0 <= int(query[k]) <= maximum):
                raise ValueError('Invalid pagination')
        for k, expected in (('money', ('all', 'at')), ('type', ('all',)), ('excode', ('oprac',)), ('callput', ('callput',))):
            if k in query and query[k] not in expected:
                raise ValueError('Invalid option filter')
        headers.update({'Origin': 'https://www.nasdaq.com', 'Referer': 'https://www.nasdaq.com/'})
        return 'https://api.nasdaq.com' + route.removeprefix('/nasdaq') + '?' + urllib.parse.urlencode(query), headers, None
    if route in ('/binance/api/v3/klines', '/binance/api/v3/ticker/24hr'):
        if not re.fullmatch(r'[A-Z0-9]{1,20}USDT', query.get('symbol', '')):
            raise ValueError('Invalid Binance pair')
        clean = {'symbol': query['symbol']}
        if route.endswith('klines'):
            if query.get('interval') not in ('1h', '1d', '1w') or query.get('limit') != '1000':
                raise ValueError('Invalid kline range')
            clean.update(interval=query['interval'], limit='1000')
        return 'https://api.binance.com' + route.removeprefix('/binance') + '?' + urllib.parse.urlencode(clean), headers, None
    if route == '/stooq/q/d/l/' and re.fullmatch(r'[a-z-]{1,10}\.us', query.get('s', '')) and query.get('i') == 'd':
        return 'https://stooq.com/q/d/l/?' + urllib.parse.urlencode({'s': query['s'], 'i': 'd'}), headers, None
    raise ValueError('Unsupported data route')


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


class Handler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if not self.path.startswith(('/yahoo/', '/nasdaq/', '/binance/', '/scanner/', '/stooq/')):
            return super().do_GET()
        self.proxy('GET')

    def do_POST(self):
        self.proxy('POST')

    def proxy(self, method):
        try:
            body = None
            if method == 'POST':
                length = int(self.headers.get('Content-Length', '0'))
                if not 0 < length <= 16384:
                    raise ValueError('Invalid request size')
                body = self.rfile.read(length)
            url, headers, data = upstream(self.path, method, body)
        except (ValueError, KeyError, TypeError, AttributeError) as error:
            return self.send_error(400, str(error))
        try:
            request = urllib.request.Request(url, headers=headers, data=data, method=method)
            with urllib.request.build_opener(NoRedirect).open(request, timeout=25) as response:
                result = response.read(64 * 1024 * 1024 + 1)
                if len(result) > 64 * 1024 * 1024:
                    return self.send_error(502, 'Response too large')
                status = response.status
        except urllib.error.HTTPError as error:
            status, result = error.code, error.read(16384)
        except (OSError, urllib.error.URLError) as error:
            return self.send_error(502, str(error))
        self.send_response(status)
        self.send_header('Content-Type', 'application/json' if '/stooq/' not in self.path else 'text/csv')
        self.send_header('Content-Length', str(len(result)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        try:
            self.wfile.write(result)
        except (BrokenPipeError, ConnectionResetError):
            pass


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
