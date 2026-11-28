#!/usr/bin/env python3
"""check-cookie-psl.py: cookies for public suffixes are refused.

    python3 scripts/check-cookie-psl.py path/to/weststar

Starts an HTTP proxy on 127.0.0.1 that answers every request with
data/fixtures/cookie-psl.html, setting through Set-Cookie a cookie for
each domain the requested host is in, and has the browser (headless, in a
fresh profile, with NS_HTTP_PROXY pointing at the proxy) load that page
from hosts that need the Public Suffix List to be told apart: an ICANN
suffix of two labels, a wildcard rule and its exception, the private
section. The page sets the same cookies through document.cookie and
prints which were kept. Every cookie must be kept, except those for a
public suffix: curl's cookie engine decides for the Set-Cookie ones, the
browser (src/psl.c) for document.cookie's. Exit status 0 when all is as
expected, 1 otherwise.
"""
import http.server
import os
import shutil
import subprocess
import sys
import tempfile
import threading

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PAGE = os.path.join(ROOT, 'data', 'fixtures', 'cookie-psl.html')

# host -> the domains of it that are public suffixes.
HOSTS = {
    'www.example.co.uk': {'co.uk', 'uk'},
    'www.example.com': {'com'},
    'a.b.foo.ck': {'foo.ck', 'ck'},          # *.ck
    'a.www.ck': {'ck'},                      # !www.ck
    'x.b.c.kawasaki.jp': {'c.kawasaki.jp', 'kawasaki.jp', 'jp'},
    'www.city.kawasaki.jp': {'kawasaki.jp', 'jp'},
    'www.example.github.io': {'github.io', 'io'},  # private section
}


def cookie_name(prefix, domain):
    return prefix + ''.join(c if c.isalnum() else '_' for c in domain)


def domains_of(host):
    labels = host.split('.')
    return ['.'.join(labels[i:]) for i in range(len(labels))]


class Proxy(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        host = (self.headers.get('Host') or '').split(':')[0].lower()
        if (host not in HOSTS or not self.path.startswith('http://')
                or not self.path.endswith('/cookie-psl.html')):
            self.send_response(404)
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        with open(PAGE, 'rb') as f:
            body = f.read()
        self.send_response(200)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.send_header('Cache-Control', 'no-store')
        for d in domains_of(host):
            self.send_header('Set-Cookie', '%s=1; Domain=%s; Path=/'
                             % (cookie_name('h_', d), d))
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        pass


def main():
    if len(sys.argv) != 2:
        sys.exit('usage: check-cookie-psl.py path/to/weststar')
    browser = os.path.abspath(sys.argv[1])
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Proxy)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    port = server.server_address[1]
    failures = 0
    for host, suffixes in HOSTS.items():
        profile = tempfile.mkdtemp(prefix='weststar-cookie-psl-')
        try:
            env = dict(os.environ)
            for var in ('XDG_CONFIG_HOME', 'XDG_DATA_HOME', 'XDG_CACHE_HOME',
                        'XDG_RUNTIME_DIR'):
                path = os.path.join(profile, var.lower())
                os.makedirs(path)
                env[var] = path
            env['HOME'] = profile
            env['NS_HTTP_PROXY'] = 'http://127.0.0.1:%d' % port
            env['NS_ALLOW_ROOT'] = '1'
            url = 'http://%s/cookie-psl.html' % host
            out = subprocess.run(
                [browser, '--headless', '--dump=text', '--settle-ms=500', url],
                env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                timeout=120).stdout.decode('utf-8', 'replace')
        finally:
            shutil.rmtree(profile, ignore_errors=True)
        seen = {}
        for line in out.splitlines():
            parts = line.split()
            if len(parts) == 4 and parts[0] == 'COOKIE' and parts[1] == host:
                seen[parts[2]] = parts[3]
        if 'END' not in out.split():
            print('FAIL %s: the page did not finish:\n%s' % (host, out))
            failures += 1
            continue
        for d in domains_of(host):
            want = 'refused' if d in suffixes else 'kept'
            for prefix in ('h_', 'js_'):
                key = prefix + 'Domain=' + d
                got = seen.get(key)
                status = 'ok  ' if got == want else 'FAIL'
                if got != want:
                    failures += 1
                print('%s %s %s: %s (want %s)' % (status, host, key, got, want))
    server.shutdown()
    if failures:
        print('%d failures' % failures)
        return 1
    print('all as expected')
    return 0


if __name__ == '__main__':
    sys.exit(main())
