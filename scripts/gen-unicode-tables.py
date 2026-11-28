#!/usr/bin/env python3
"""Generate src/core/ns-unicode-tables.h from the Unicode Character Database.

nscore (src/core/) answers the Unicode questions the engine used to ask
GLib: general category, script, case mappings, case folding and
normalization. This script builds its tables from the UCD of the version
pinned below, so a build needs neither GLib nor the network:

    python3 scripts/gen-unicode-tables.py CACHE_DIR \
        src/core/ns-unicode-tables.h

The UCD files are downloaded from unicode.org into CACHE_DIR when they are
not there yet (UnicodeData.txt, CaseFolding.txt, SpecialCasing.txt,
Scripts.txt, CompositionExclusions.txt, DerivedNormalizationProps.txt;
NormalizationTest.txt too, which only the tests read).

The tables follow what GLib's gen-unicode-tables.pl makes of the same
files, so that nscore answers as GLib does: the categories and scripts are
numbered as GUnicodeType and GUnicodeScript; the simple case mappings are
kept for letters only (Lu, Ll, Lt); SpecialCasing.txt's unconditional
entries give the full mappings of letters; CaseFolding.txt's C and F
entries are kept where they differ from the simple lowercase mapping; the
decompositions are expanded fully; the primary composites are the
canonical pairs that are not Full_Composition_Exclusion.
"""
import os
import re
import sys
import urllib.request

UCD_VERSION = '17.0.0'
UCD_URL = 'https://www.unicode.org/Public/%s/ucd/' % UCD_VERSION
FILES = [
    'UnicodeData.txt', 'CaseFolding.txt', 'SpecialCasing.txt',
    'Scripts.txt', 'CompositionExclusions.txt',
    'DerivedNormalizationProps.txt', 'NormalizationTest.txt',
]
MAX_CP = 0x110000

# GUnicodeType's order, by General_Category value.
TYPES = [
    'Cc', 'Cf', 'Cn', 'Co', 'Cs', 'Ll', 'Lm', 'Lo', 'Lt', 'Lu', 'Mc', 'Me',
    'Mn', 'Nd', 'Nl', 'No', 'Pc', 'Pd', 'Pe', 'Pf', 'Pi', 'Po', 'Ps', 'Sc',
    'Sk', 'Sm', 'So', 'Zl', 'Zp', 'Zs',
]

# GUnicodeScript's order (GLib 2.90), from 0; Scripts.txt's names
# upper-cased. MATH (Zmth) is no Script value; it is never returned.
SCRIPTS = '''
COMMON INHERITED ARABIC ARMENIAN BENGALI BOPOMOFO CHEROKEE COPTIC CYRILLIC
DESERET DEVANAGARI ETHIOPIC GEORGIAN GOTHIC GREEK GUJARATI GURMUKHI HAN
HANGUL HEBREW HIRAGANA KANNADA KATAKANA KHMER LAO LATIN MALAYALAM MONGOLIAN
MYANMAR OGHAM OLD_ITALIC ORIYA RUNIC SINHALA SYRIAC TAMIL TELUGU THAANA THAI
TIBETAN CANADIAN_ABORIGINAL YI TAGALOG HANUNOO BUHID TAGBANWA BRAILLE
CYPRIOT LIMBU OSMANYA SHAVIAN LINEAR_B TAI_LE UGARITIC NEW_TAI_LUE BUGINESE
GLAGOLITIC TIFINAGH SYLOTI_NAGRI OLD_PERSIAN KHAROSHTHI UNKNOWN BALINESE
CUNEIFORM PHOENICIAN PHAGS_PA NKO KAYAH_LI LEPCHA REJANG SUNDANESE
SAURASHTRA CHAM OL_CHIKI VAI CARIAN LYCIAN LYDIAN AVESTAN BAMUM
EGYPTIAN_HIEROGLYPHS IMPERIAL_ARAMAIC INSCRIPTIONAL_PAHLAVI
INSCRIPTIONAL_PARTHIAN JAVANESE KAITHI LISU MEETEI_MAYEK OLD_SOUTH_ARABIAN
OLD_TURKIC SAMARITAN TAI_THAM TAI_VIET BATAK BRAHMI MANDAIC CHAKMA
MEROITIC_CURSIVE MEROITIC_HIEROGLYPHS MIAO SHARADA SORA_SOMPENG TAKRI
BASSA_VAH CAUCASIAN_ALBANIAN DUPLOYAN ELBASAN GRANTHA KHOJKI KHUDAWADI
LINEAR_A MAHAJANI MANICHAEAN MENDE_KIKAKUI MODI MRO NABATAEAN
OLD_NORTH_ARABIAN OLD_PERMIC PAHAWH_HMONG PALMYRENE PAU_CIN_HAU
PSALTER_PAHLAVI SIDDHAM TIRHUTA WARANG_CITI AHOM ANATOLIAN_HIEROGLYPHS
HATRAN MULTANI OLD_HUNGARIAN SIGNWRITING ADLAM BHAIKSUKI MARCHEN NEWA OSAGE
TANGUT MASARAM_GONDI NUSHU SOYOMBO ZANABAZAR_SQUARE DOGRA GUNJALA_GONDI
HANIFI_ROHINGYA MAKASAR MEDEFAIDRIN OLD_SOGDIAN SOGDIAN ELYMAIC NANDINAGARI
NYIAKENG_PUACHUE_HMONG WANCHO CHORASMIAN DIVES_AKURU KHITAN_SMALL_SCRIPT
YEZIDI CYPRO_MINOAN OLD_UYGHUR TANGSA TOTO VITHKUQI MATH KAWI NAG_MUNDARI
TODHRI GARAY TULU_TIGALARI SUNUWAR GURUNG_KHEMA KIRAT_RAI OL_ONAL SIDETIC
TOLONG_SIKI TAI_YO BERIA_ERFE
'''.split()

SBASE, SCOUNT = 0xAC00, 11172


def fetch(cache):
    os.makedirs(cache, exist_ok=True)
    for name in FILES:
        path = os.path.join(cache, name)
        if not os.path.exists(path):
            print('downloading %s%s' % (UCD_URL, name), file=sys.stderr)
            with urllib.request.urlopen(UCD_URL + name) as r:
                data = r.read()
            with open(path + '.part', 'wb') as f:
                f.write(data)
            os.replace(path + '.part', path)


def lines(cache, name, check_version=True):
    path = os.path.join(cache, name)
    with open(path, encoding='utf-8') as f:
        text = f.read()
    if check_version:
        stem = name[:-4]
        m = re.search(r'%s-([\d.]+)\.txt' % re.escape(stem), text[:4000])
        if not m or m.group(1) != UCD_VERSION:
            sys.exit('expected %s-%s.txt in %s' % (stem, UCD_VERSION, cache))
    for line in text.splitlines():
        line = line.split('#', 1)[0].strip()
        if line:
            yield [field.strip() for field in line.split(';')]


def cps(field):
    return [int(x, 16) for x in field.split()]


def two_stage(values, elem_bits):
    """Return (shift, stage1, stage2) of the smallest two-stage table."""
    best = None
    for shift in range(4, 11):
        size = 1 << shift
        blocks, stage1, stage2 = {}, [], []
        for start in range(0, MAX_CP, size):
            block = tuple(values[start:start + size])
            if block not in blocks:
                blocks[block] = len(blocks)
                stage2.extend(block)
            stage1.append(blocks[block])
        s1_bytes = 1 if len(blocks) <= 256 else 2
        total = len(stage1) * s1_bytes + len(stage2) * elem_bits // 8
        if best is None or total < best[0]:
            best = (total, shift, stage1, stage2)
    return best[1:]


def emit_array(f, ctype, name, values, per_line=16):
    f.write('static const %s %s[%d] = {\n' % (ctype, name, len(values)))
    for i in range(0, len(values), per_line):
        f.write('  ' + ','.join('%d' % v for v in values[i:i + per_line]) +
                ',\n')
    f.write('};\n\n')


def emit_two_stage(f, prefix, values):
    shift, stage1, stage2 = two_stage(values, 8)
    s1type = 'uint8_t' if max(stage1) < 256 else 'uint16_t'
    f.write('#define %s_SHIFT %d\n\n' % (prefix.upper(), shift))
    emit_array(f, s1type, prefix + '_stage1', stage1, 20)
    emit_array(f, 'uint8_t', prefix + '_stage2', stage2, 24)


def main():
    if len(sys.argv) != 3:
        sys.exit('usage: gen-unicode-tables.py CACHE_DIR OUTPUT.h')
    cache, out = sys.argv[1], sys.argv[2]
    fetch(cache)

    # UnicodeData.txt: category, combining class, decomposition, simple
    # case mappings.
    tindex = {t: i for i, t in enumerate(TYPES)}
    gc = [tindex['Cn']] * MAX_CP
    ccc = [0] * MAX_CP
    decomp = {}          # cp -> (is_compat, [cps])
    upper, lower, title = {}, {}, {}
    first = None
    for fields in lines(cache, 'UnicodeData.txt', check_version=False):
        cp = int(fields[0], 16)
        name = fields[1]
        if name.endswith(', First>'):
            first = cp
            continue
        lo = first if name.endswith(', Last>') else cp
        first = None
        for c in range(lo, cp + 1):
            gc[c] = tindex[fields[2]]
            ccc[c] = int(fields[3])
        if fields[5]:
            parts = fields[5].split()
            compat = parts[0].startswith('<')
            decomp[cp] = (compat, cps(' '.join(parts[1:] if compat else parts)))
        if fields[12]:
            upper[cp] = int(fields[12], 16)
        if fields[13]:
            lower[cp] = int(fields[13], 16)
        if fields[14]:
            title[cp] = int(fields[14], 16)

    LU, LL, LT = tindex['Lu'], tindex['Ll'], tindex['Lt']

    # Simple mappings as GLib keeps them: a lowercase letter's uppercase,
    # an uppercase letter's lowercase; titlecase letters in their own
    # table of (title, upper, lower).
    simple = {}
    for cp in range(MAX_CP):
        if gc[cp] == LL and upper.get(cp):
            simple[cp] = upper[cp]
        elif gc[cp] == LU and lower.get(cp):
            simple[cp] = lower[cp]
    titles = [(cp, upper.get(cp, 0), lower.get(cp, 0))
              for cp in range(MAX_CP) if gc[cp] == LT]

    def glib_tolower(cp):
        if gc[cp] == LU:
            return simple.get(cp, cp)
        if gc[cp] == LT:
            for t in titles:
                if t[0] == cp:
                    return t[2]
        return cp

    # SpecialCasing.txt, unconditional entries of letters.
    special_upper, special_lower = {}, {}
    for fields in lines(cache, 'SpecialCasing.txt'):
        if len(fields) > 4 and fields[4]:
            continue                      # conditional
        cp = int(fields[0], 16)
        slower, supper = cps(fields[1]), cps(fields[3])
        if gc[cp] == LL:
            special_upper[cp] = supper
        elif gc[cp] == LU:
            special_lower[cp] = slower
        elif gc[cp] == LT:
            special_upper[cp] = supper
            special_lower[cp] = slower

    # CaseFolding.txt: C and F entries unless the same as tolower.
    casefold = {}
    for fields in lines(cache, 'CaseFolding.txt'):
        cp, status, mapping = int(fields[0], 16), fields[1], cps(fields[2])
        if status not in ('C', 'F'):
            continue
        if len(mapping) == 1 and cp not in special_lower and \
                mapping[0] == glib_tolower(cp):
            continue
        casefold[cp] = mapping

    # Scripts.txt.
    sindex = {s: i for i, s in enumerate(SCRIPTS)}
    script = [sindex['UNKNOWN']] * MAX_CP
    for fields in lines(cache, 'Scripts.txt'):
        rng, name = fields[0], fields[1].upper()
        if name not in sindex:
            sys.exit('script %s is not in GUnicodeScript' % name)
        lo, _, hi = rng.partition('..')
        lo = int(lo, 16)
        hi = int(hi, 16) if hi else lo
        for c in range(lo, hi + 1):
            script[c] = sindex[name]

    # Full decompositions.
    def expand(cp, compat):
        d = decomp.get(cp)
        if d is None or (d[0] and not compat):
            return [cp]
        res = []
        for c in d[1]:
            res.extend(expand(c, compat))
        return res

    decomps = []
    for cp in sorted(decomp):
        canon = expand(cp, False) if not decomp[cp][0] else None
        compat = expand(cp, True)
        for seq in (canon or []) + compat:
            if SBASE <= seq < SBASE + SCOUNT:
                sys.exit('U+%04X decomposes to a Hangul syllable' % cp)
        decomps.append((cp, canon, compat))

    # Compositions.
    exclusions = set()
    for fields in lines(cache, 'CompositionExclusions.txt'):
        exclusions.add(int(fields[0], 16))
    full_exclusion = set()
    for fields in lines(cache, 'DerivedNormalizationProps.txt'):
        if fields[1] == 'Full_Composition_Exclusion':
            lo, _, hi = fields[0].partition('..')
            lo = int(lo, 16)
            hi = int(hi, 16) if hi else lo
            full_exclusion.update(range(lo, hi + 1))
    compose = []
    derived = set(exclusions)
    for cp, (compat, seq) in decomp.items():
        if compat:
            continue
        if len(seq) == 1 or ccc[cp] != 0 or ccc[seq[0]] != 0:
            derived.add(cp)
            continue
        if cp not in exclusions:
            compose.append((seq[0], seq[1], cp))
    if derived != full_exclusion:
        sys.exit('composition exclusions differ from '
                 'Full_Composition_Exclusion')
    compose.sort()

    # One pool of code point sequences, each ended by 0.
    pool = [0]
    seen = {}

    def intern(seq):
        if seq is None:
            return 0
        key = tuple(seq)
        if key not in seen:
            seen[key] = len(pool)
            pool.extend(seq)
            pool.append(0)
        return seen[key]

    special = []
    for cp in sorted(set(special_upper) | set(special_lower)):
        special.append((cp, intern(special_upper.get(cp)),
                        intern(special_lower.get(cp))))
    folds = [(cp, intern(casefold[cp])) for cp in sorted(casefold)]
    decomp_rows = [(cp, intern(canon), intern(compat))
                   for cp, canon, compat in decomps]
    if len(pool) > 0xFFFF:
        sys.exit('sequence pool too large for 16-bit offsets')

    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('/* Generated by scripts/gen-unicode-tables.py from the Unicode\n'
                ' * Character Database %s. Do not edit.\n'
                ' * Copyright 1991-2025 Unicode, Inc.; see\n'
                ' * https://www.unicode.org/license.txt (Unicode License v3).\n'
                ' * SPDX-License-Identifier: Unicode-3.0\n'
                ' */\n\n' % UCD_VERSION)
        f.write('#define NS_UNICODE_UCD_VERSION "%s"\n\n' % UCD_VERSION)
        f.write('/* Two-stage tables: value of cp is\n'
                ' * stage2[(stage1[cp >> SHIFT] << SHIFT) + (cp & ((1 << SHIFT) - 1))]. */\n\n')
        f.write('/* General category, as NsUnicodeType. */\n')
        emit_two_stage(f, 'ns_uc_type', gc)
        f.write('/* Script, as NsUnicodeScript. */\n')
        emit_two_stage(f, 'ns_uc_script', script)
        f.write('/* Canonical combining class. */\n')
        emit_two_stage(f, 'ns_uc_ccc', ccc)

        f.write('/* Sequences of code points, each ended by 0; offset 0 is none. */\n')
        emit_array(f, 'uint32_t', 'ns_uc_pool', pool, 10)

        f.write('/* Simple case mapping of letters: an Ll\'s uppercase, an Lu\'s\n'
                ' * lowercase. Sorted by code point. */\n')
        keys = sorted(simple)
        f.write('#define NS_UC_SIMPLE_N %d\n' % len(keys))
        emit_array(f, 'uint32_t', 'ns_uc_simple_cp', keys, 10)
        emit_array(f, 'uint32_t', 'ns_uc_simple_map', [simple[k] for k in keys], 10)

        f.write('/* Titlecase letters: title, upper, lower (0: none). */\n')
        f.write('#define NS_UC_TITLE_N %d\n' % len(titles))
        f.write('static const uint32_t ns_uc_title[NS_UC_TITLE_N][3] = {\n')
        for t in titles:
            f.write('  {0x%04X, 0x%04X, 0x%04X},\n' % t)
        f.write('};\n\n')

        f.write('/* SpecialCasing.txt, unconditional: code point, offsets of the\n'
                ' * full uppercase and lowercase in ns_uc_pool. */\n')
        f.write('#define NS_UC_SPECIAL_N %d\n' % len(special))
        f.write('static const uint32_t ns_uc_special[NS_UC_SPECIAL_N][3] = {\n')
        for s in special:
            f.write('  {0x%04X, %d, %d},\n' % s)
        f.write('};\n\n')

        f.write('/* Full case folding where it is not the simple lowercase:\n'
                ' * code point, offset in ns_uc_pool. */\n')
        f.write('#define NS_UC_FOLD_N %d\n' % len(folds))
        f.write('static const uint32_t ns_uc_fold[NS_UC_FOLD_N][2] = {\n')
        for i in range(0, len(folds), 6):
            f.write('  ' + ' '.join('{0x%04X,%d},' % r for r in folds[i:i + 6]) + '\n')
        f.write('};\n\n')

        f.write('/* Full decompositions (Hangul syllables aside): code point,\n'
                ' * offsets of the canonical (0: compatibility only) and the\n'
                ' * compatibility decomposition in ns_uc_pool. */\n')
        f.write('#define NS_UC_DECOMP_N %d\n' % len(decomp_rows))
        f.write('static const uint32_t ns_uc_decomp[NS_UC_DECOMP_N][3] = {\n')
        for i in range(0, len(decomp_rows), 4):
            f.write('  ' + ' '.join('{0x%04X,%d,%d},' % r for r in decomp_rows[i:i + 4]) + '\n')
        f.write('};\n\n')

        f.write('/* Primary composites: first, second, composite; sorted. */\n')
        f.write('#define NS_UC_COMPOSE_N %d\n' % len(compose))
        f.write('static const uint32_t ns_uc_compose[NS_UC_COMPOSE_N][3] = {\n')
        for i in range(0, len(compose), 4):
            f.write('  ' + ' '.join('{0x%04X,0x%04X,0x%04X},' % r for r in compose[i:i + 4]) + '\n')
        f.write('};\n')


if __name__ == '__main__':
    main()
