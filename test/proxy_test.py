"""Validate the browser proxy without accessing the network."""
import importlib.util
import json
import pathlib
import unittest
spec = importlib.util.spec_from_file_location('serve_web', pathlib.Path(__file__).parents[1] / 'scripts/serve_web.py')
server = importlib.util.module_from_spec(spec)
spec.loader.exec_module(server)

class ProxyTest(unittest.TestCase):
    def test_fixed_routes(self):
        url, headers, body = server.upstream('/nasdaq/api/quote/SPY/option-chain?assetclass=etf&limit=2000&offset=2000&fromdate=2026-09-23&todate=2026-11-23')
        self.assertTrue(url.startswith('https://api.nasdaq.com/api/quote/SPY/option-chain?'))
        self.assertEqual(headers['Origin'], 'https://www.nasdaq.com')
        self.assertIsNone(body)
        url, _, _ = server.upstream('/yahoo/v8/finance/chart/%5EVIX?interval=1h&period1=123&period2=456')
        self.assertIn('includePrePost=false', url)
        url, _, _ = server.upstream('/binance/api/v3/klines?symbol=BTCUSDT&interval=1d&limit=1000')
        self.assertTrue(url.startswith('https://api.binance.com/'))

    def test_expiry_discovery(self):
        url, _, _ = server.upstream('/nasdaq/api/quote/SPY/option-chain?assetclass=etf&limit=1&fromdate=all&money=all')
        self.assertIn('limit=1', url)
        url, _, _ = server.upstream('/nasdaq/api/quote/SPY/option-chain?assetclass=etf&limit=2000&fromdate=all&money=at')
        self.assertIn('money=at', url)
        with self.assertRaises(ValueError):
            server.upstream('/nasdaq/api/quote/SPY/chart?assetclass=etf&fromdate=all')

    def test_rejects_unbounded_routes(self):
        for route in ['/nasdaq/../../anything', '/nasdaq/api/quote/SPY/option-chain?assetclass=etf&limit=999999',
                      '/nasdaq/api/quote/SPY/info?assetclass=etf&assetclass=stocks',
                      '/binance/api/v3/order?symbol=BTCUSDT',
                      '/yahoo/v8/finance/chart/http://evil.com?interval=1d&period1=0&period2=123',
                      '/nasdaq/api/quote/SPY/chart?assetclass=etf&fromdate=wrong',
                      'https://evil.com/x']:
            with self.subTest(route=route), self.assertRaises(ValueError):
                server.upstream(route)

    def test_scanner(self):
        request = {'columns': ['description', 'close', 'change', 'market_cap_basic', 'sector', 'volume', 'exchange'],
                   'range': [0, 100], 'sort': {'sortBy': 'change', 'sortOrder': 'desc'},
                   'filter': [{'left': 'type', 'operation': 'equal', 'right': 'stock'}]}
        url, headers, body = server.upstream('/scanner/america/scan', 'POST', json.dumps(request))
        self.assertEqual(url, 'https://scanner.tradingview.com/america/scan')
        self.assertEqual(json.loads(body), request)
        request['range'][1] = 10000
        with self.assertRaises(ValueError):
            server.upstream('/scanner/america/scan', 'POST', json.dumps(request))
        with self.assertRaises(ValueError):
            server.upstream('/scanner/america/other', 'POST', '{}')

if __name__ == '__main__':
    unittest.main()
