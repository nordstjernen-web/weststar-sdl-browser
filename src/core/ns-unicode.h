/* Weststar SDL Browser — nscore: UTF-8, UTF-16 and Unicode properties.
 *
 * The functions are GLib's (g_utf8_*, g_unichar_*, g_utf16_*) under the
 * ns_ prefix, with GLib 2.80's signatures, results and enum values, so
 * that src/core/glib.h can map one onto the other. The tables are of
 * Unicode 17.0 (ns-unicode-tables.h, made by scripts/gen-unicode-tables.py).
 *
 * Where GLib asks the C library's locale, nscore does not:
 *  - ns_utf8_strup / ns_utf8_strdown apply the default (language-neutral)
 *    rules of SpecialCasing.txt; GLib also has the Turkish/Azeri and
 *    Lithuanian rules for an LC_CTYPE of tr, az or lt.
 *  - ns_utf8_collate / ns_utf8_collate_key do not use strcoll/strxfrm.
 *    They compare NFKD forms on three levels: first the letters with the
 *    marks removed and the case folded, then with the marks, then the
 *    NFKD form itself (code point order: uppercase before lowercase).
 *    The order is deterministic and the same everywhere;
 *    strcmp(collate_key(a), collate_key(b)) has the sign of
 *    collate(a, b).
 *
 * Strings returned are allocated with ns_malloc and freed with ns_free.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_UNICODE_H
#define NS_UNICODE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t ns_unichar;       /* gunichar */
typedef uint16_t ns_unichar2;      /* gunichar2 */

/* GUnicodeType. */
typedef enum {
  NS_UNICODE_CONTROL,
  NS_UNICODE_FORMAT,
  NS_UNICODE_UNASSIGNED,
  NS_UNICODE_PRIVATE_USE,
  NS_UNICODE_SURROGATE,
  NS_UNICODE_LOWERCASE_LETTER,
  NS_UNICODE_MODIFIER_LETTER,
  NS_UNICODE_OTHER_LETTER,
  NS_UNICODE_TITLECASE_LETTER,
  NS_UNICODE_UPPERCASE_LETTER,
  NS_UNICODE_SPACING_MARK,
  NS_UNICODE_ENCLOSING_MARK,
  NS_UNICODE_NON_SPACING_MARK,
  NS_UNICODE_DECIMAL_NUMBER,
  NS_UNICODE_LETTER_NUMBER,
  NS_UNICODE_OTHER_NUMBER,
  NS_UNICODE_CONNECT_PUNCTUATION,
  NS_UNICODE_DASH_PUNCTUATION,
  NS_UNICODE_CLOSE_PUNCTUATION,
  NS_UNICODE_FINAL_PUNCTUATION,
  NS_UNICODE_INITIAL_PUNCTUATION,
  NS_UNICODE_OTHER_PUNCTUATION,
  NS_UNICODE_OPEN_PUNCTUATION,
  NS_UNICODE_CURRENCY_SYMBOL,
  NS_UNICODE_MODIFIER_SYMBOL,
  NS_UNICODE_MATH_SYMBOL,
  NS_UNICODE_OTHER_SYMBOL,
  NS_UNICODE_LINE_SEPARATOR,
  NS_UNICODE_PARAGRAPH_SEPARATOR,
  NS_UNICODE_SPACE_SEPARATOR
} NsUnicodeType;
#define NS_UNICODE_COMBINING_MARK NS_UNICODE_SPACING_MARK

/* GUnicodeScript, as of GLib 2.90 (Unicode 17). */
typedef enum {
  NS_UNICODE_SCRIPT_INVALID_CODE = -1,
  NS_UNICODE_SCRIPT_COMMON = 0,
  NS_UNICODE_SCRIPT_INHERITED,
  NS_UNICODE_SCRIPT_ARABIC,
  NS_UNICODE_SCRIPT_ARMENIAN,
  NS_UNICODE_SCRIPT_BENGALI,
  NS_UNICODE_SCRIPT_BOPOMOFO,
  NS_UNICODE_SCRIPT_CHEROKEE,
  NS_UNICODE_SCRIPT_COPTIC,
  NS_UNICODE_SCRIPT_CYRILLIC,
  NS_UNICODE_SCRIPT_DESERET,
  NS_UNICODE_SCRIPT_DEVANAGARI,
  NS_UNICODE_SCRIPT_ETHIOPIC,
  NS_UNICODE_SCRIPT_GEORGIAN,
  NS_UNICODE_SCRIPT_GOTHIC,
  NS_UNICODE_SCRIPT_GREEK,
  NS_UNICODE_SCRIPT_GUJARATI,
  NS_UNICODE_SCRIPT_GURMUKHI,
  NS_UNICODE_SCRIPT_HAN,
  NS_UNICODE_SCRIPT_HANGUL,
  NS_UNICODE_SCRIPT_HEBREW,
  NS_UNICODE_SCRIPT_HIRAGANA,
  NS_UNICODE_SCRIPT_KANNADA,
  NS_UNICODE_SCRIPT_KATAKANA,
  NS_UNICODE_SCRIPT_KHMER,
  NS_UNICODE_SCRIPT_LAO,
  NS_UNICODE_SCRIPT_LATIN,
  NS_UNICODE_SCRIPT_MALAYALAM,
  NS_UNICODE_SCRIPT_MONGOLIAN,
  NS_UNICODE_SCRIPT_MYANMAR,
  NS_UNICODE_SCRIPT_OGHAM,
  NS_UNICODE_SCRIPT_OLD_ITALIC,
  NS_UNICODE_SCRIPT_ORIYA,
  NS_UNICODE_SCRIPT_RUNIC,
  NS_UNICODE_SCRIPT_SINHALA,
  NS_UNICODE_SCRIPT_SYRIAC,
  NS_UNICODE_SCRIPT_TAMIL,
  NS_UNICODE_SCRIPT_TELUGU,
  NS_UNICODE_SCRIPT_THAANA,
  NS_UNICODE_SCRIPT_THAI,
  NS_UNICODE_SCRIPT_TIBETAN,
  NS_UNICODE_SCRIPT_CANADIAN_ABORIGINAL,
  NS_UNICODE_SCRIPT_YI,
  NS_UNICODE_SCRIPT_TAGALOG,
  NS_UNICODE_SCRIPT_HANUNOO,
  NS_UNICODE_SCRIPT_BUHID,
  NS_UNICODE_SCRIPT_TAGBANWA,
  NS_UNICODE_SCRIPT_BRAILLE,
  NS_UNICODE_SCRIPT_CYPRIOT,
  NS_UNICODE_SCRIPT_LIMBU,
  NS_UNICODE_SCRIPT_OSMANYA,
  NS_UNICODE_SCRIPT_SHAVIAN,
  NS_UNICODE_SCRIPT_LINEAR_B,
  NS_UNICODE_SCRIPT_TAI_LE,
  NS_UNICODE_SCRIPT_UGARITIC,
  NS_UNICODE_SCRIPT_NEW_TAI_LUE,
  NS_UNICODE_SCRIPT_BUGINESE,
  NS_UNICODE_SCRIPT_GLAGOLITIC,
  NS_UNICODE_SCRIPT_TIFINAGH,
  NS_UNICODE_SCRIPT_SYLOTI_NAGRI,
  NS_UNICODE_SCRIPT_OLD_PERSIAN,
  NS_UNICODE_SCRIPT_KHAROSHTHI,
  NS_UNICODE_SCRIPT_UNKNOWN,
  NS_UNICODE_SCRIPT_BALINESE,
  NS_UNICODE_SCRIPT_CUNEIFORM,
  NS_UNICODE_SCRIPT_PHOENICIAN,
  NS_UNICODE_SCRIPT_PHAGS_PA,
  NS_UNICODE_SCRIPT_NKO,
  NS_UNICODE_SCRIPT_KAYAH_LI,
  NS_UNICODE_SCRIPT_LEPCHA,
  NS_UNICODE_SCRIPT_REJANG,
  NS_UNICODE_SCRIPT_SUNDANESE,
  NS_UNICODE_SCRIPT_SAURASHTRA,
  NS_UNICODE_SCRIPT_CHAM,
  NS_UNICODE_SCRIPT_OL_CHIKI,
  NS_UNICODE_SCRIPT_VAI,
  NS_UNICODE_SCRIPT_CARIAN,
  NS_UNICODE_SCRIPT_LYCIAN,
  NS_UNICODE_SCRIPT_LYDIAN,
  NS_UNICODE_SCRIPT_AVESTAN,
  NS_UNICODE_SCRIPT_BAMUM,
  NS_UNICODE_SCRIPT_EGYPTIAN_HIEROGLYPHS,
  NS_UNICODE_SCRIPT_IMPERIAL_ARAMAIC,
  NS_UNICODE_SCRIPT_INSCRIPTIONAL_PAHLAVI,
  NS_UNICODE_SCRIPT_INSCRIPTIONAL_PARTHIAN,
  NS_UNICODE_SCRIPT_JAVANESE,
  NS_UNICODE_SCRIPT_KAITHI,
  NS_UNICODE_SCRIPT_LISU,
  NS_UNICODE_SCRIPT_MEETEI_MAYEK,
  NS_UNICODE_SCRIPT_OLD_SOUTH_ARABIAN,
  NS_UNICODE_SCRIPT_OLD_TURKIC,
  NS_UNICODE_SCRIPT_SAMARITAN,
  NS_UNICODE_SCRIPT_TAI_THAM,
  NS_UNICODE_SCRIPT_TAI_VIET,
  NS_UNICODE_SCRIPT_BATAK,
  NS_UNICODE_SCRIPT_BRAHMI,
  NS_UNICODE_SCRIPT_MANDAIC,
  NS_UNICODE_SCRIPT_CHAKMA,
  NS_UNICODE_SCRIPT_MEROITIC_CURSIVE,
  NS_UNICODE_SCRIPT_MEROITIC_HIEROGLYPHS,
  NS_UNICODE_SCRIPT_MIAO,
  NS_UNICODE_SCRIPT_SHARADA,
  NS_UNICODE_SCRIPT_SORA_SOMPENG,
  NS_UNICODE_SCRIPT_TAKRI,
  NS_UNICODE_SCRIPT_BASSA_VAH,
  NS_UNICODE_SCRIPT_CAUCASIAN_ALBANIAN,
  NS_UNICODE_SCRIPT_DUPLOYAN,
  NS_UNICODE_SCRIPT_ELBASAN,
  NS_UNICODE_SCRIPT_GRANTHA,
  NS_UNICODE_SCRIPT_KHOJKI,
  NS_UNICODE_SCRIPT_KHUDAWADI,
  NS_UNICODE_SCRIPT_LINEAR_A,
  NS_UNICODE_SCRIPT_MAHAJANI,
  NS_UNICODE_SCRIPT_MANICHAEAN,
  NS_UNICODE_SCRIPT_MENDE_KIKAKUI,
  NS_UNICODE_SCRIPT_MODI,
  NS_UNICODE_SCRIPT_MRO,
  NS_UNICODE_SCRIPT_NABATAEAN,
  NS_UNICODE_SCRIPT_OLD_NORTH_ARABIAN,
  NS_UNICODE_SCRIPT_OLD_PERMIC,
  NS_UNICODE_SCRIPT_PAHAWH_HMONG,
  NS_UNICODE_SCRIPT_PALMYRENE,
  NS_UNICODE_SCRIPT_PAU_CIN_HAU,
  NS_UNICODE_SCRIPT_PSALTER_PAHLAVI,
  NS_UNICODE_SCRIPT_SIDDHAM,
  NS_UNICODE_SCRIPT_TIRHUTA,
  NS_UNICODE_SCRIPT_WARANG_CITI,
  NS_UNICODE_SCRIPT_AHOM,
  NS_UNICODE_SCRIPT_ANATOLIAN_HIEROGLYPHS,
  NS_UNICODE_SCRIPT_HATRAN,
  NS_UNICODE_SCRIPT_MULTANI,
  NS_UNICODE_SCRIPT_OLD_HUNGARIAN,
  NS_UNICODE_SCRIPT_SIGNWRITING,
  NS_UNICODE_SCRIPT_ADLAM,
  NS_UNICODE_SCRIPT_BHAIKSUKI,
  NS_UNICODE_SCRIPT_MARCHEN,
  NS_UNICODE_SCRIPT_NEWA,
  NS_UNICODE_SCRIPT_OSAGE,
  NS_UNICODE_SCRIPT_TANGUT,
  NS_UNICODE_SCRIPT_MASARAM_GONDI,
  NS_UNICODE_SCRIPT_NUSHU,
  NS_UNICODE_SCRIPT_SOYOMBO,
  NS_UNICODE_SCRIPT_ZANABAZAR_SQUARE,
  NS_UNICODE_SCRIPT_DOGRA,
  NS_UNICODE_SCRIPT_GUNJALA_GONDI,
  NS_UNICODE_SCRIPT_HANIFI_ROHINGYA,
  NS_UNICODE_SCRIPT_MAKASAR,
  NS_UNICODE_SCRIPT_MEDEFAIDRIN,
  NS_UNICODE_SCRIPT_OLD_SOGDIAN,
  NS_UNICODE_SCRIPT_SOGDIAN,
  NS_UNICODE_SCRIPT_ELYMAIC,
  NS_UNICODE_SCRIPT_NANDINAGARI,
  NS_UNICODE_SCRIPT_NYIAKENG_PUACHUE_HMONG,
  NS_UNICODE_SCRIPT_WANCHO,
  NS_UNICODE_SCRIPT_CHORASMIAN,
  NS_UNICODE_SCRIPT_DIVES_AKURU,
  NS_UNICODE_SCRIPT_KHITAN_SMALL_SCRIPT,
  NS_UNICODE_SCRIPT_YEZIDI,
  NS_UNICODE_SCRIPT_CYPRO_MINOAN,
  NS_UNICODE_SCRIPT_OLD_UYGHUR,
  NS_UNICODE_SCRIPT_TANGSA,
  NS_UNICODE_SCRIPT_TOTO,
  NS_UNICODE_SCRIPT_VITHKUQI,
  NS_UNICODE_SCRIPT_MATH,
  NS_UNICODE_SCRIPT_KAWI,
  NS_UNICODE_SCRIPT_NAG_MUNDARI,
  NS_UNICODE_SCRIPT_TODHRI,
  NS_UNICODE_SCRIPT_GARAY,
  NS_UNICODE_SCRIPT_TULU_TIGALARI,
  NS_UNICODE_SCRIPT_SUNUWAR,
  NS_UNICODE_SCRIPT_GURUNG_KHEMA,
  NS_UNICODE_SCRIPT_KIRAT_RAI,
  NS_UNICODE_SCRIPT_OL_ONAL,
  NS_UNICODE_SCRIPT_SIDETIC,
  NS_UNICODE_SCRIPT_TOLONG_SIKI,
  NS_UNICODE_SCRIPT_TAI_YO,
  NS_UNICODE_SCRIPT_BERIA_ERFE
} NsUnicodeScript;

/* GNormalizeMode. */
typedef enum {
  NS_NORMALIZE_DEFAULT,
  NS_NORMALIZE_NFD = NS_NORMALIZE_DEFAULT,
  NS_NORMALIZE_DEFAULT_COMPOSE,
  NS_NORMALIZE_NFC = NS_NORMALIZE_DEFAULT_COMPOSE,
  NS_NORMALIZE_ALL,
  NS_NORMALIZE_NFKD = NS_NORMALIZE_ALL,
  NS_NORMALIZE_ALL_COMPOSE,
  NS_NORMALIZE_NFKC = NS_NORMALIZE_ALL_COMPOSE
} NsNormalizeMode;

/* From the rest of nscore: NsError, ns_error_new_literal, the GConvertError
 * codes ns_utf8_to_utf16 / ns_utf16_to_utf8 set (NS_CONVERT_ERROR_*) with
 * ns_convert_error_quark, and ns_malloc / ns_free. */
#include "ns-error.h"
#include "ns-mem.h"

/* g_utf8_skip: the length of a UTF-8 sequence by its first byte. */
extern const char ns_utf8_skip[256];
#define ns_utf8_next_char(p) \
  ((char *)((p) + ns_utf8_skip[*(const unsigned char *)(p)]))

/* Code points. Booleans are int, 0 or 1, as gboolean. */
int ns_unichar_validate(ns_unichar ch);
int ns_unichar_to_utf8(ns_unichar c, char *outbuf);
NsUnicodeType ns_unichar_type(ns_unichar c);
NsUnicodeScript ns_unichar_get_script(ns_unichar ch);
int ns_unichar_isalnum(ns_unichar c);
int ns_unichar_isalpha(ns_unichar c);
int ns_unichar_iscntrl(ns_unichar c);
int ns_unichar_isdigit(ns_unichar c);
int ns_unichar_ismark(ns_unichar c);
int ns_unichar_isprint(ns_unichar c);
int ns_unichar_isspace(ns_unichar c);
int ns_unichar_isupper(ns_unichar c);
int ns_unichar_islower(ns_unichar c);
ns_unichar ns_unichar_toupper(ns_unichar c);
ns_unichar ns_unichar_tolower(ns_unichar c);
ns_unichar ns_unichar_totitle(ns_unichar c);
int ns_unichar_combining_class(ns_unichar uc);

/* UTF-8. A length or max_len < 0 means up to the NUL. */
ns_unichar ns_utf8_get_char(const char *p);
ns_unichar ns_utf8_get_char_validated(const char *p, ptrdiff_t max_len);
char *ns_utf8_prev_char(const char *p);
long ns_utf8_strlen(const char *p, ptrdiff_t max);
char *ns_utf8_offset_to_pointer(const char *str, long offset);
long ns_utf8_pointer_to_offset(const char *str, const char *pos);
int ns_utf8_validate(const char *str, ptrdiff_t max_len, const char **end);
int ns_utf8_validate_len(const char *str, size_t max_len, const char **end);
char *ns_utf8_make_valid(const char *str, ptrdiff_t len);

/* Case, normalization and collation; results from ns_malloc. */
char *ns_utf8_strup(const char *str, ptrdiff_t len);
char *ns_utf8_strdown(const char *str, ptrdiff_t len);
char *ns_utf8_casefold(const char *str, ptrdiff_t len);
char *ns_utf8_normalize(const char *str, ptrdiff_t len, NsNormalizeMode mode);
int ns_utf8_collate(const char *str1, const char *str2);
char *ns_utf8_collate_key(const char *str, ptrdiff_t len);

/* UTF-16 (len, items_read, items_written: as glong in GLib). */
ns_unichar2 *ns_utf8_to_utf16(const char *str, long len, long *items_read,
                              long *items_written, NsError **error);
char *ns_utf16_to_utf8(const ns_unichar2 *str, long len, long *items_read,
                       long *items_written, NsError **error);

#ifdef __cplusplus
}
#endif

#endif /* NS_UNICODE_H */
