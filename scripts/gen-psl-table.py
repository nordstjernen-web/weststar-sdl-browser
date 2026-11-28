#!/usr/bin/env python3
"""Generate src/psl-table.h, the Public Suffix List compiled into the browser.

src/psl.c answers the two questions the engine asks of the Public Suffix
List (is this a public suffix, and what is the registrable domain of this
host) from a table this script makes, so that a build needs neither libpsl
nor the network:

    python3 scripts/gen-psl-table.py CACHE_DIR src/psl-table.h

public_suffix_list.dat of the commit of github.com/publicsuffix/list
pinned below is downloaded into CACHE_DIR when it is not there yet, and
checked against the SHA-256 recorded here. To move to a newer list, set
PSL_COMMIT, PSL_DATE and PSL_SHA256 to its commit, run the script and
commit the header together with the change.

The rules are read as libpsl's psl-make-dafsa reads them for psl_builtin():
both sections (ICANN and private); "!rule" an exception, "*.rule" a
wildcard (which also makes "rule" itself a public suffix), other "*" rules
ignored; each rule kept as written and, where it has non-ASCII labels, in
its A-label (punycode) form too, made by Python's IDNA codec as libpsl's
generator makes it. A rule met twice keeps both meanings (its flags are
OR-ed, as libpsl's own list loader does).

The table holds each rule's bytes reversed, sorted, in blocks of 16
front-coded entries (each entry a byte with its flags in the top two bits
and in the low six the length it shares with the entry before, a byte
with the length of the rest, and the rest); an index gives where
each block starts. src/psl.c finds a rule by a binary search over the
blocks' first entries and a scan of one block.

The generated header is a derived work of the list, which is under the
Mozilla Public License 2.0; see THIRD-PARTY-LICENSES.md.
"""
import hashlib
import os
import sys
import urllib.request

PSL_COMMIT = 'a2d308560551fc115bc1ffcac4f68992748012c4'
PSL_DATE = '2026-11-28'
PSL_SHA256 = 'af287ecae41a1df72363c1877cf99ce9499df5d1bbbb96d32cdb1c555fc3891a'
PSL_URL = ('https://raw.githubusercontent.com/publicsuffix/list/'
           + PSL_COMMIT + '/public_suffix_list.dat')

FLAG_EXCEPTION = 1
FLAG_WILDCARD = 2
BLOCK = 16


def fetch(cache_dir):
    os.makedirs(cache_dir, exist_ok=True)
    path = os.path.join(cache_dir, 'public_suffix_list-%s.dat' % PSL_COMMIT[:12])
    if not os.path.exists(path):
        print('downloading', PSL_URL, file=sys.stderr)
        with urllib.request.urlopen(PSL_URL) as r:
            data = r.read()
        with open(path + '.tmp', 'wb') as f:
            f.write(data)
        os.replace(path + '.tmp', path)
    with open(path, 'rb') as f:
        data = f.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != PSL_SHA256:
        sys.exit('%s: SHA-256 %s, expected %s' % (path, digest, PSL_SHA256))
    return data


def parse(data):
    rules = {}
    counts = {'rules': 0, 'exceptions': 0, 'wildcards': 0}
    for raw in data.decode('utf-8').splitlines():
        line = raw.strip()
        if not line or line.startswith('//'):
            continue
        # libpsl takes a rule up to the first white space.
        line = line.split()[0]
        if line.startswith('!'):
            flags = FLAG_EXCEPTION
            rule = line[1:]
            counts['exceptions'] += 1
        elif line.startswith('*'):
            if not line.startswith('*.'):
                print('unsupported rule (ignored):', line, file=sys.stderr)
                continue
            flags = FLAG_WILDCARD
            rule = line[2:]
            counts['wildcards'] += 1
        else:
            flags = 0
            rule = line
        counts['rules'] += 1
        forms = [rule.encode('utf-8')]
        ascii_form = rule.encode('idna')
        if ascii_form not in forms:
            forms.append(ascii_form)
        for form in forms:
            rules[form] = rules.get(form, 0) | flags
    return rules, counts


def encode(rules):
    entries = sorted((bytes(reversed(k)), v) for k, v in rules.items())
    blob = bytearray()
    index = []
    prev = b''
    for i, (key, flags) in enumerate(entries):
        if len(key) > 63:
            sys.exit('rule too long for the table: %r' % key)
        if i % BLOCK == 0:
            index.append(len(blob))
            shared = 0
        else:
            shared = 0
            while (shared < len(key) and shared < len(prev)
                   and key[shared] == prev[shared]):
                shared += 1
        rest = key[shared:]
        blob += bytes([shared | flags << 6, len(rest)]) + rest
        prev = key
    return entries, blob, index


def c_bytes(data, per_line=16):
    out = []
    for i in range(0, len(data), per_line):
        out.append('    ' + ','.join('%d' % b for b in data[i:i + per_line]) + ',')
    return '\n'.join(out)


def main():
    if len(sys.argv) != 3:
        sys.exit('usage: gen-psl-table.py CACHE_DIR OUTPUT')
    data = fetch(sys.argv[1])
    rules, counts = parse(data)
    entries, blob, index = encode(rules)
    max_len = max(len(k) for k, _ in entries)
    with open(sys.argv[2], 'w', encoding='utf-8', newline='\n') as f:
        f.write('/* Generated by scripts/gen-psl-table.py; do not edit. */\n')
        f.write('/*\n')
        f.write(' * The Public Suffix List (https://publicsuffix.org/),\n')
        f.write(' * github.com/publicsuffix/list commit %s\n' % PSL_COMMIT)
        f.write(' * of %s, public_suffix_list.dat SHA-256\n' % PSL_DATE)
        f.write(' * %s.\n' % PSL_SHA256)
        f.write(' *\n')
        f.write(' * This Source Code Form is subject to the terms of the Mozilla Public\n')
        f.write(' * License, v. 2.0. If a copy of the MPL was not distributed with this\n')
        f.write(' * file, You can obtain one at https://mozilla.org/MPL/2.0/.\n')
        f.write(' *\n')
        f.write(' * %d rules (%d wildcards, %d exceptions), %d entries with the\n'
                % (counts['rules'], counts['wildcards'], counts['exceptions'],
                   len(entries)))
        f.write(' * A-label forms; %d bytes in %d blocks.\n' % (len(blob), len(index)))
        f.write(' */\n')
        f.write('#define NS_PSL_COMMIT "%s"\n' % PSL_COMMIT)
        f.write('#define NS_PSL_DATE "%s"\n' % PSL_DATE)
        f.write('#define NS_PSL_ENTRIES %d\n' % len(entries))
        f.write('#define NS_PSL_BLOCK %d\n' % BLOCK)
        f.write('#define NS_PSL_MAX_LEN %d\n' % max_len)
        f.write('#define NS_PSL_FLAG_EXCEPTION %d\n' % FLAG_EXCEPTION)
        f.write('#define NS_PSL_FLAG_WILDCARD %d\n\n' % FLAG_WILDCARD)
        f.write('static const unsigned char ns_psl_blob[%d] = {\n' % len(blob))
        f.write(c_bytes(blob))
        f.write('\n};\n\n')
        f.write('static const unsigned int ns_psl_index[%d] = {\n' % len(index))
        for i in range(0, len(index), 8):
            f.write('    ' + ','.join('%d' % v for v in index[i:i + 8]) + ',\n')
        f.write('};\n')
    print('%d entries, %d bytes, %d blocks' % (len(entries), len(blob), len(index)),
          file=sys.stderr)


if __name__ == '__main__':
    main()
