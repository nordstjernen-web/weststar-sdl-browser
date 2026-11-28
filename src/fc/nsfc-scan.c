/* Weststar SDL Browser — nsfc: finding the fonts.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The fonts are the files in the platform's font folders, opened with
 * FreeType once and remembered in a cache file, so that later starts only
 * list the folders. After them come the fonts compiled into the browser
 * (-Dbundled_fonts, nsfc-bundled.c), if any.
 */

#include "nsfc-private.h"

#include "brand.h"
#include "plat/ns_env.h"
#include "plat/ns_fs.h"
#include "plat/ns_plat.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_FONT_FORMATS_H
#include FT_MULTIPLE_MASTERS_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_IDS_H
#include FT_TRUETYPE_TABLES_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Guards the FreeType library used for scanning, and the configuration. */
static NsMutex g_lock;
static FT_Library g_ft;
static FcConfig *g_config;

/* ---- reading one font file ---- */

typedef struct ScanStream {
    FT_StreamRec rec;
    FILE        *file;
} ScanStream;

static unsigned long
scan_stream_read(FT_Stream stream, unsigned long offset, unsigned char *buffer,
                 unsigned long count)
{
    ScanStream *s = stream->descriptor.pointer;
    if (!s->file || fseek(s->file, (long)offset, SEEK_SET) != 0)
        return count ? 0 : 1; /* a seek reports failure as non-zero */
    if (!count)
        return 0;
    return (unsigned long)fread(buffer, 1, count, s->file);
}

static void
scan_stream_close(FT_Stream stream)
{
    ScanStream *s = stream->descriptor.pointer;
    if (s->file) {
        fclose(s->file);
        s->file = NULL;
    }
}

/* Opens face `index` of a font file (-1: only to count its faces). The
 * file is read through ns_fs, which takes UTF-8 paths on every platform. */
static FT_Face
scan_open(const char *path, Uint64 size, long index, ScanStream **stream_out)
{
    ScanStream *s = SDL_calloc(1, sizeof *s);
    if (!s) return NULL;
    s->file = ns_fs_fopen(path, "rb");
    if (!s->file) {
        SDL_free(s);
        return NULL;
    }
    s->rec.size = (unsigned long)size;
    s->rec.descriptor.pointer = s;
    s->rec.read = scan_stream_read;
    s->rec.close = scan_stream_close;

    FT_Open_Args args = { .flags = FT_OPEN_STREAM, .stream = &s->rec };
    FT_Face face = NULL;
    if (FT_Open_Face(g_ft, &args, index, &face) != 0) {
        scan_stream_close(&s->rec);
        SDL_free(s);
        return NULL;
    }
    *stream_out = s;
    return face;
}

static void
scan_close(FT_Face face, ScanStream *s)
{
    FT_Done_Face(face);
    scan_stream_close(&s->rec);
    SDL_free(s);
}

/* A name-table string in UTF-16BE, as UTF-8. */
static char *
utf16be_to_utf8(const FT_Byte *in, unsigned len)
{
    char *out = SDL_malloc((size_t)len / 2 * 3 + 1);
    size_t o = 0;
    if (!out) return NULL;
    for (unsigned i = 0; i + 1 < len; i += 2) {
        Uint32 c = (Uint32)in[i] << 8 | in[i + 1];
        if (c >= 0xd800 && c < 0xdc00 && i + 3 < len) {
            Uint32 low = (Uint32)in[i + 2] << 8 | in[i + 3];
            if (low >= 0xdc00 && low < 0xe000) {
                c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
                i += 2;
            }
        }
        if (c >= 0xd800 && c < 0xe000) c = 0xfffd;
        if (!c) break;
        if (c < 0x80) {
            out[o++] = (char)c;
        } else if (c < 0x800) {
            out[o++] = (char)(0xc0 | c >> 6);
            out[o++] = (char)(0x80 | (c & 0x3f));
        } else if (c < 0x10000) {
            out[o++] = (char)(0xe0 | c >> 12);
            out[o++] = (char)(0x80 | (c >> 6 & 0x3f));
            out[o++] = (char)(0x80 | (c & 0x3f));
        } else {
            out[o++] = (char)(0xf0 | c >> 18);
            out[o++] = (char)(0x80 | (c >> 12 & 0x3f));
            out[o++] = (char)(0x80 | (c >> 6 & 0x3f));
            out[o++] = (char)(0x80 | (c & 0x3f));
        }
    }
    out[o] = 0;
    return out;
}

static void
add_family(FcPattern *p, const char *family)
{
    const NsFcElt *e = nsfc_pattern_find(p, FC_FAMILY);
    if (!family || !*family) return;
    for (int i = 0; e && i < e->n; i++)
        if (strcmp(e->values[i].u.s, family) == 0)
            return;
    FcPatternAddString(p, FC_FAMILY, (const FcChar8 *)family);
}

/* Adds the family names of one kind from the name table: the typographic
 * family (which groups all the weights of a family) or the legacy one
 * (which names "Inter Black" a family of its own), in English or in the
 * other languages. */
static void
add_table_families(FcPattern *p, FT_Face face, FT_UShort name_id, bool english)
{
    FT_UInt count = FT_Get_Sfnt_Name_Count(face);
    for (FT_UInt i = 0; i < count; i++) {
        FT_SfntName name;
        if (FT_Get_Sfnt_Name(face, i, &name) != 0 || name.name_id != name_id)
            continue;
        if (name.platform_id != TT_PLATFORM_MICROSOFT ||
            (name.encoding_id != TT_MS_ID_UNICODE_CS &&
             name.encoding_id != TT_MS_ID_UCS_4))
            continue;
        if ((name.language_id == TT_MS_LANGID_ENGLISH_UNITED_STATES) != english)
            continue;
        char *utf8 = utf16be_to_utf8(name.string, name.string_len);
        add_family(p, utf8);
        SDL_free(utf8);
    }
}

/* Every family name of a face, so that a page can ask for the font by any
 * of them. The first is the one the family is listed under: the
 * typographic family where the font has one, as in fontconfig. */
static void
add_families(FcPattern *p, FT_Face face)
{
    add_table_families(p, face, TT_NAME_ID_TYPOGRAPHIC_FAMILY, true);
    add_family(p, face->family_name);
    add_table_families(p, face, TT_NAME_ID_FONT_FAMILY, true);
    add_table_families(p, face, TT_NAME_ID_TYPOGRAPHIC_FAMILY, false);
    add_table_families(p, face, TT_NAME_ID_FONT_FAMILY, false);
}

static bool
contains_word(const char *s, const char *word)
{
    return s && SDL_strcasestr(s, word) != NULL;
}

/* The pattern of an open face, or NULL when the text stack cannot use it. */
static FcPattern *
scan_face(FT_Face face, const char *path, int index)
{
    /* HarfBuzz shapes SFNT fonts only; bitmap strikes are used for colour
     * glyphs (emoji) alone. */
    if (!FT_IS_SFNT(face) || (!FT_IS_SCALABLE(face) && !FT_HAS_COLOR(face)))
        return NULL;
    if (!face->family_name || !*face->family_name)
        return NULL;

    FcPattern *p = FcPatternCreate();
    if (!p) return NULL;

    add_families(p, face);
    const char *style = face->style_name && *face->style_name
                            ? face->style_name : "Regular";
    FcPatternAddString(p, FC_STYLE, (const FcChar8 *)style);
    FcPatternAddString(p, FC_FILE, (const FcChar8 *)path);
    FcPatternAddInteger(p, FC_INDEX, index);

    double ot_weight = face->style_flags & FT_STYLE_FLAG_BOLD ? 700 : 400;
    int width = FC_WIDTH_NORMAL;
    int slant = face->style_flags & FT_STYLE_FLAG_ITALIC ? FC_SLANT_ITALIC
                                                         : FC_SLANT_ROMAN;
    const TT_OS2 *os2 = FT_Get_Sfnt_Table(face, FT_SFNT_OS2);
    if (os2 && os2->version != 0xffff) {
        static const int widths[] = {
            FC_WIDTH_ULTRACONDENSED, FC_WIDTH_EXTRACONDENSED,
            FC_WIDTH_CONDENSED, FC_WIDTH_SEMICONDENSED, FC_WIDTH_NORMAL,
            FC_WIDTH_SEMIEXPANDED, FC_WIDTH_EXPANDED, FC_WIDTH_EXTRAEXPANDED,
            FC_WIDTH_ULTRAEXPANDED,
        };
        if (os2->usWeightClass >= 1 && os2->usWeightClass <= 9)
            ot_weight = os2->usWeightClass * 100; /* an old convention */
        else if (os2->usWeightClass >= 10 && os2->usWeightClass <= 1000)
            ot_weight = os2->usWeightClass;
        if (os2->usWidthClass >= 1 && os2->usWidthClass <= 9)
            width = widths[os2->usWidthClass - 1];
        if (os2->fsSelection & (1 << 9))
            slant = FC_SLANT_OBLIQUE;
    }
    if (slant == FC_SLANT_ITALIC && contains_word(style, "oblique"))
        slant = FC_SLANT_OBLIQUE;
    double weight = FcWeightFromOpenTypeDouble(ot_weight);

    FcPatternAddDouble(p, FC_WEIGHT, weight);
    FcPatternAddInteger(p, FC_SLANT, slant);
    FcPatternAddInteger(p, FC_WIDTH, width);
    if (FT_IS_FIXED_WIDTH(face))
        FcPatternAddInteger(p, FC_SPACING, FC_MONO);
    FcPatternAddBool(p, FC_SCALABLE, FcTrue);
    FcPatternAddBool(p, FC_OUTLINE, FT_IS_SCALABLE(face) ? FcTrue : FcFalse);
    FcPatternAddBool(p, FC_COLOR, FT_HAS_COLOR(face) ? FcTrue : FcFalse);
    /* A variable font is one face here, with the range of its weight
     * axis; FcFontRenderPrepare asks it for the requested weight. */
    FcPatternAddBool(p, FC_VARIABLE, FcFalse);
    if (FT_HAS_MULTIPLE_MASTERS(face)) {
        FT_MM_Var *mm = NULL;
        if (FT_Get_MM_Var(face, &mm) == 0 && mm) {
            for (FT_UInt i = 0; i < mm->num_axis; i++) {
                if (mm->axis[i].tag != FT_MAKE_TAG('w', 'g', 'h', 't'))
                    continue;
                double lo = (double)mm->axis[i].minimum / 65536.0;
                double hi = (double)mm->axis[i].maximum / 65536.0;
                if (hi > lo) {
                    FcPatternAddDouble(p, NSFC_WEIGHT_MIN,
                                       FcWeightFromOpenTypeDouble(lo));
                    FcPatternAddDouble(p, NSFC_WEIGHT_MAX,
                                       FcWeightFromOpenTypeDouble(hi));
                }
            }
            FT_Done_MM_Var(g_ft, mm);
        }
    }

    const char *format = FT_Get_Font_Format(face);
    if (format)
        FcPatternAddString(p, FC_FONTFORMAT, (const FcChar8 *)format);
    FcPatternAddString(p, FC_FONT_WRAPPER, (const FcChar8 *)"SFNT");

    FcCharSet *chars = FcCharSetCreate();
    if (chars) {
        if (face->charmap && face->charmap->encoding == FT_ENCODING_UNICODE) {
            FT_UInt glyph = 0;
            FT_ULong c = FT_Get_First_Char(face, &glyph);
            while (glyph != 0) {
                FcCharSetAddChar(chars, (FcChar32)c);
                c = FT_Get_Next_Char(face, c, &glyph);
            }
        }
        nsfc_charset_set_constant(chars);
        nsfc_pattern_add(p, FC_CHARSET,
            (NsFcValue){ .type = FcTypeCharSet, .u.c = chars }, true);
    }
    return p;
}

/* Called with the lock held. */
static bool
ensure_freetype(void)
{
    return g_ft || FT_Init_FreeType(&g_ft) == 0;
}

static int
scan_file_locked(const char *path, int only_face, int *n_faces, FcFontSet *set)
{
    NsFsStat st;
    ScanStream *stream = NULL;
    int added = 0;

    if (n_faces) *n_faces = 0;
    if (!ensure_freetype() || ns_fs_stat(path, &st) != 0 ||
        st.type != SDL_PATHTYPE_FILE)
        return 0;

    FT_Face probe = scan_open(path, st.size, -1, &stream);
    if (!probe) return 0;
    long count = probe->num_faces;
    scan_close(probe, stream);
    if (count > 0xffff) count = 0xffff;
    if (n_faces) *n_faces = (int)count;

    for (long i = 0; i < count; i++) {
        if (only_face >= 0 && i != only_face)
            continue;
        FT_Face face = scan_open(path, st.size, i, &stream);
        if (!face) continue;
        FcPattern *p = scan_face(face, path, (int)i);
        scan_close(face, stream);
        if (p && FcFontSetAdd(set, p))
            added++;
        else
            FcPatternDestroy(p);
    }
    return added;
}

int
nsfc_scan_file(const char *path, int only_face, int *n_faces, FcFontSet *set)
{
    ns_mutex_lock(&g_lock);
    int added = scan_file_locked(path, only_face, n_faces, set);
    ns_mutex_unlock(&g_lock);
    return added;
}

FcPattern *
FcFreeTypeQuery(const FcChar8 *file, unsigned int id, FcBlanks *blanks, int *count)
{
    (void)blanks;
    FcFontSet *set = FcFontSetCreate();
    FcPattern *p = NULL;
    if (!set) return NULL;
    if (nsfc_scan_file((const char *)file, (int)(id & 0xffff), count, set) > 0) {
        p = set->fonts[0];
        FcPatternReference(p);
    }
    FcFontSetDestroy(set);
    return p;
}

unsigned int
FcFreeTypeQueryAll(const FcChar8 *file, unsigned int id, FcBlanks *blanks,
                   int *count, FcFontSet *set)
{
    (void)blanks;
    int only = id == (unsigned int)-1 ? -1 : (int)(id & 0xffff);
    if (!set) return 0;
    return (unsigned int)nsfc_scan_file((const char *)file, only, count, set);
}

/* ---- the font folders ---- */

typedef struct StrList {
    char **items;
    int    n, cap;
} StrList;

static void
str_list_add(StrList *list, const char *s)
{
    if (!s || !*s) return;
    for (int i = 0; i < list->n; i++)
        if (strcmp(list->items[i], s) == 0)
            return;
    if (list->n == list->cap) {
        int cap = list->cap ? list->cap * 2 : 32;
        char **items = SDL_realloc(list->items, (size_t)cap * sizeof *items);
        if (!items) return;
        list->items = items;
        list->cap = cap;
    }
    char *copy = SDL_strdup(s);
    if (copy) list->items[list->n++] = copy;
}

static void
str_list_add_joined(StrList *list, const char *dir, const char *name)
{
    char *path = NULL;
    if (!dir || !*dir) return;
    size_t len = strlen(dir);
    const char *sep = dir[len - 1] == '/' || dir[len - 1] == '\\' ||
                      dir[len - 1] == ':' ? "" : "/";
    if (SDL_asprintf(&path, "%s%s%s", dir, sep, name) >= 0) {
        str_list_add(list, path);
        SDL_free(path);
    }
}

static void
str_list_clear(StrList *list)
{
    for (int i = 0; i < list->n; i++)
        SDL_free(list->items[i]);
    SDL_free(list->items);
    *list = (StrList){ 0 };
}

/* Each entry of a path list (':' between entries, ';' on Windows), with
 * `suffix` appended when given. */
static void
add_path_list(StrList *dirs, const char *value, const char *suffix)
{
#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    while (value && *value) {
        const char *end = strchr(value, sep);
        size_t len = end ? (size_t)(end - value) : strlen(value);
        char *entry = SDL_strndup(value, len);
        if (entry && *entry) {
            if (suffix) str_list_add_joined(dirs, entry, suffix);
            else str_list_add(dirs, entry);
        }
        SDL_free(entry);
        value = end ? end + 1 : NULL;
    }
}

/* Whether NS_NO_SYSTEM_FONTS is set (and not empty, 0, false, no or off):
 * then only NS_FONT_PATH and the bundled fonts are used, which is how a
 * test sees what the browser does on a system without fonts. */
static bool
no_system_fonts(void)
{
    const char *v = ns_getenv("NS_NO_SYSTEM_FONTS");
    return v && *v && strcmp(v, "0") != 0 && SDL_strcasecmp(v, "false") != 0 &&
           SDL_strcasecmp(v, "no") != 0 && SDL_strcasecmp(v, "off") != 0;
}

/* Where the platform keeps its fonts, then a "fonts" folder beside the
 * executable, then the folders named in NS_FONT_PATH. */
static void
font_dirs(StrList *dirs)
{
    if (no_system_fonts()) {
        add_path_list(dirs, ns_getenv("NS_FONT_PATH"), NULL);
        return;
    }
#if defined(_WIN32)
    const char *windir = ns_getenv("WINDIR");
    if (!windir) windir = ns_getenv("SystemRoot");
    str_list_add_joined(dirs, windir ? windir : "C:/Windows", "Fonts");
    str_list_add_joined(dirs, ns_getenv("LOCALAPPDATA"), "Microsoft/Windows/Fonts");
#elif defined(__APPLE__)
    str_list_add(dirs, "/System/Library/Fonts");
    str_list_add(dirs, "/Library/Fonts");
    str_list_add_joined(dirs, ns_fs_home_dir(), "Library/Fonts");
#elif defined(__amigaos4__) || defined(__MORPHOS__) || defined(__AROS__) || \
      defined(__amigaos__)
    str_list_add(dirs, "FONTS:");
#elif defined(__HAIKU__)
    str_list_add(dirs, "/boot/system/data/fonts");
    str_list_add(dirs, "/boot/system/non-packaged/data/fonts");
    str_list_add(dirs, "/boot/home/config/non-packaged/data/fonts");
#else
    str_list_add(dirs, "/usr/share/fonts");
    str_list_add(dirs, "/usr/local/share/fonts");
    str_list_add(dirs, "/usr/X11R6/lib/X11/fonts");
    str_list_add(dirs, "/usr/X11R7/lib/X11/fonts");
    str_list_add(dirs, "/usr/pkg/share/fonts");
    add_path_list(dirs, ns_getenv("XDG_DATA_DIRS"), "fonts");
    str_list_add_joined(dirs, ns_fs_user_data_dir(), "fonts");
    str_list_add_joined(dirs, ns_fs_home_dir(), ".fonts");
#endif
    str_list_add_joined(dirs, SDL_GetBasePath(), "fonts");
    add_path_list(dirs, ns_getenv("NS_FONT_PATH"), NULL);
}

static bool
is_font_file(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && (SDL_strcasecmp(dot, ".ttf") == 0 ||
                   SDL_strcasecmp(dot, ".otf") == 0 ||
                   SDL_strcasecmp(dot, ".ttc") == 0 ||
                   SDL_strcasecmp(dot, ".otc") == 0);
}

static int
cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* The font files under a folder, in a fixed order: a folder's files by
 * name, then its subfolders by name. */
static void
list_font_files(const char *dir, int depth, StrList *files)
{
    StrList names = { 0 }, subdirs = { 0 };
    NsDir *d = ns_dir_open(dir);
    const char *name;
    if (!d) return;
    while ((name = ns_dir_read_name(d)) != NULL) {
        if (name[0] == '.') continue;
        str_list_add_joined(&names, dir, name);
    }
    ns_dir_close(d);
    if (names.n)
        qsort(names.items, (size_t)names.n, sizeof *names.items, cmp_str);
    for (int i = 0; i < names.n; i++) {
        NsFsStat st;
        if (ns_fs_stat(names.items[i], &st) != 0) continue;
        if (st.type == SDL_PATHTYPE_DIRECTORY)
            str_list_add(&subdirs, names.items[i]);
        else if (st.type == SDL_PATHTYPE_FILE && is_font_file(names.items[i]))
            str_list_add(files, names.items[i]);
    }
    /* Deep enough for any font folder, and an end to a loop of links. */
    for (int i = 0; depth < 8 && i < subdirs.n; i++)
        list_font_files(subdirs.items[i], depth + 1, files);
    str_list_clear(&names);
    str_list_clear(&subdirs);
}

/* ---- the cache ---- */

#define CACHE_MAGIC   0x4346534eu /* "NSFC" */
#define CACHE_VERSION 2u

typedef struct CacheEntry {
    const char *path;   /* in the cache file's bytes */
    Uint64      mtime, size;
    FcFontSet  *fonts;  /* the file's faces; empty for a file with none */
    bool        used;
} CacheEntry;

typedef struct Cache {
    Uint8      *data;
    CacheEntry *entries;
    int         n;
} Cache;

static char *
cache_path(void)
{
    char *path = NULL;
    const char *dir = ns_fs_user_cache_dir();
    if (!dir || !*dir) return NULL;
    if (SDL_asprintf(&path, "%s/%s/fonts.cache", dir, NS_APP_DIR_NAME) < 0)
        return NULL;
    return path;
}

static int
cmp_entry(const void *a, const void *b)
{
    return strcmp(((const CacheEntry *)a)->path, ((const CacheEntry *)b)->path);
}

static void
cache_free(Cache *cache)
{
    for (int i = 0; i < cache->n; i++)
        FcFontSetDestroy(cache->entries[i].fonts);
    SDL_free(cache->entries);
    SDL_free(cache->data);
    *cache = (Cache){ 0 };
}

static bool
read_u64(const Uint8 **cursor, const Uint8 *end, Uint64 *v)
{
    Uint32 hi, lo;
    if (!nsfc_read_u32(cursor, end, &hi) || !nsfc_read_u32(cursor, end, &lo))
        return false;
    *v = (Uint64)hi << 32 | lo;
    return true;
}

static void
put_u64(NsFcBuf *buf, Uint64 v)
{
    nsfc_buf_put_u32(buf, (Uint32)(v >> 32));
    nsfc_buf_put_u32(buf, (Uint32)v);
}

/* Reads the cache file. Anything unexpected in it empties the cache: the
 * fonts are then scanned again and the file rewritten. */
static void
cache_load(Cache *cache)
{
    char *path = cache_path();
    char *contents = NULL;
    size_t length = 0;
    *cache = (Cache){ 0 };
    if (!path) return;
    bool ok = ns_fs_get_contents(path, &contents, &length);
    SDL_free(path);
    if (!ok) return;
    cache->data = (Uint8 *)contents;

    const Uint8 *cursor = cache->data, *end = cache->data + length;
    Uint32 magic, version, word, count;
    if (!nsfc_read_u32(&cursor, end, &magic) || magic != CACHE_MAGIC ||
        !nsfc_read_u32(&cursor, end, &version) || version != CACHE_VERSION ||
        !nsfc_read_u32(&cursor, end, &word) || word != sizeof(void *) ||
        !nsfc_read_u32(&cursor, end, &count) || count > 1000000)
        goto fail;
    cache->entries = SDL_calloc(count ? count : 1, sizeof *cache->entries);
    if (!cache->entries) goto fail;
    for (Uint32 i = 0; i < count; i++) {
        CacheEntry *e = &cache->entries[i];
        Uint32 n_fonts;
        e->path = nsfc_read_str(&cursor, end);
        e->fonts = FcFontSetCreate();
        cache->n = (int)i + 1;
        if (!e->path || !e->fonts || !read_u64(&cursor, end, &e->mtime) ||
            !read_u64(&cursor, end, &e->size) ||
            !nsfc_read_u32(&cursor, end, &n_fonts) || n_fonts > 0xffff)
            goto fail;
        for (Uint32 j = 0; j < n_fonts; j++) {
            FcPattern *p = nsfc_pattern_read(&cursor, end);
            if (!p) goto fail;
            if (!FcFontSetAdd(e->fonts, p)) {
                FcPatternDestroy(p);
                goto fail;
            }
        }
    }
    qsort(cache->entries, (size_t)cache->n, sizeof *cache->entries, cmp_entry);
    return;
fail:
    cache_free(cache);
}

static void
cache_write_entry(NsFcBuf *buf, const char *path, const NsFsStat *st,
                  const FcFontSet *fonts)
{
    nsfc_buf_put_str(buf, path);
    put_u64(buf, (Uint64)st->mtime);
    put_u64(buf, st->size);
    nsfc_buf_put_u32(buf, (Uint32)fonts->nfont);
    for (int i = 0; i < fonts->nfont; i++)
        nsfc_pattern_write(buf, fonts->fonts[i]);
}

/* Called with the lock held. */
static void
scan_system_locked(FcFontSet *set)
{
    StrList dirs = { 0 }, files = { 0 };
    Cache cache;
    NsFcBuf out = { 0 };
    bool dirty = false;
    int reused = 0;

    font_dirs(&dirs);
    for (int i = 0; i < dirs.n; i++)
        list_font_files(dirs.items[i], 0, &files);
    cache_load(&cache);

    nsfc_buf_put_u32(&out, CACHE_MAGIC);
    nsfc_buf_put_u32(&out, CACHE_VERSION);
    nsfc_buf_put_u32(&out, (Uint32)sizeof(void *));
    nsfc_buf_put_u32(&out, (Uint32)files.n);

    for (int i = 0; i < files.n; i++) {
        NsFsStat st;
        CacheEntry key = { .path = files.items[i] };
        FcFontSet *fresh = NULL;
        const FcFontSet *fonts;
        if (ns_fs_stat(files.items[i], &st) != 0)
            st = (NsFsStat){ 0 };
        CacheEntry *hit = cache.n ? bsearch(&key, cache.entries, (size_t)cache.n,
                                            sizeof *cache.entries, cmp_entry)
                                  : NULL;
        if (hit && !hit->used && hit->mtime == (Uint64)st.mtime &&
            hit->size == st.size) {
            hit->used = true;
            reused++;
            fonts = hit->fonts;
        } else {
            fresh = FcFontSetCreate();
            if (!fresh) continue;
            scan_file_locked(files.items[i], -1, NULL, fresh);
            fonts = fresh;
            dirty = true;
        }
        cache_write_entry(&out, files.items[i], &st, fonts);
        for (int j = 0; j < fonts->nfont; j++) {
            FcPatternReference(fonts->fonts[j]);
            if (!FcFontSetAdd(set, fonts->fonts[j]))
                FcPatternDestroy(fonts->fonts[j]);
        }
        FcFontSetDestroy(fresh);
    }
    if (reused != cache.n)
        dirty = true; /* fonts that went away */

    /* A list without the system's fonts is not one to keep. */
    if (dirty && !out.failed && !no_system_fonts()) {
        char *path = cache_path();
        if (path) {
            char *slash = strrchr(path, '/');
            *slash = 0;
            ns_fs_mkdir_p(path, 0700);
            *slash = '/';
            ns_fs_set_contents(path, (const char *)out.data, (ptrdiff_t)out.len);
            SDL_free(path);
        }
    }
    SDL_free(out.data);
    cache_free(&cache);
    str_list_clear(&dirs);
    str_list_clear(&files);
}

/* ---- the bundled fonts ---- */

/* Whether the first `n` fonts of the set have a face of the same family
 * and style as `p`. */
static bool
has_face(const FcFontSet *set, int n, FcPattern *p)
{
    FcChar8 *family, *style, *f, *s;
    if (FcPatternGetString(p, FC_FAMILY, 0, &family) != FcResultMatch ||
        FcPatternGetString(p, FC_STYLE, 0, &style) != FcResultMatch)
        return false;
    for (int i = 0; i < n; i++) {
        if (FcPatternGetString(set->fonts[i], FC_FAMILY, 0, &f) == FcResultMatch &&
            FcPatternGetString(set->fonts[i], FC_STYLE, 0, &s) == FcResultMatch &&
            nsfc_family_cmp((const char *)f, (const char *)family) == 0 &&
            SDL_strcasecmp((const char *)s, (const char *)style) == 0)
            return true;
    }
    return false;
}

/* Appends the fonts compiled into the browser, marked as bundled, but for
 * the faces the system has itself. They are read from memory on every
 * start; the cache is for the files. Called with the lock held. */
static void
scan_bundled_locked(FcFontSet *set)
{
    int n_system = set->nfont;
    if (!nsfc_bundled_fonts[0].name || !ensure_freetype())
        return;
    for (const NsFcBundledFont *b = nsfc_bundled_fonts; b->name; b++) {
        char *path = NULL;
        long count = 1;
        if (SDL_asprintf(&path, "%s%s", NSFC_BUNDLED_PREFIX, b->name) < 0)
            continue;
        for (long i = 0; i < count && i <= 0xffff; i++) {
            FT_Face face = NULL;
            if (FT_New_Memory_Face(g_ft, b->data, (FT_Long)b->len, i, &face) != 0)
                break;
            count = face->num_faces;
            FcPattern *p = scan_face(face, path, (int)i);
            FT_Done_Face(face);
            if (p && !has_face(set, n_system, p) &&
                FcPatternAddBool(p, NSFC_BUNDLED, FcTrue) && FcFontSetAdd(set, p))
                continue;
            FcPatternDestroy(p);
        }
        SDL_free(path);
    }
}

/* ---- the configuration ---- */

/* The default configuration, made on first use and kept for the life of
 * the process. */
static FcConfig *
default_config(void)
{
    ns_mutex_lock(&g_lock);
    if (!g_config) {
        FcConfig *config = SDL_calloc(1, sizeof *config);
        if (config) {
            SDL_SetAtomicInt(&config->ref, 1);
            config->sets[FcSetSystem] = FcFontSetCreate();
            config->sets[FcSetApplication] = FcFontSetCreate();
            if (config->sets[FcSetSystem] && config->sets[FcSetApplication]) {
                scan_system_locked(config->sets[FcSetSystem]);
                scan_bundled_locked(config->sets[FcSetSystem]);
                g_config = config;
            } else {
                FcFontSetDestroy(config->sets[FcSetSystem]);
                FcFontSetDestroy(config->sets[FcSetApplication]);
                SDL_free(config);
            }
        }
    }
    FcConfig *config = g_config;
    ns_mutex_unlock(&g_lock);
    return config;
}

FcBool
FcInit(void)
{
    return default_config() != NULL;
}

FcConfig *
FcConfigReference(FcConfig *config)
{
    if (!config) config = default_config();
    if (config) SDL_AtomicIncRef(&config->ref);
    return config;
}

void
FcConfigDestroy(FcConfig *config)
{
    /* The default configuration holds a reference of its own, so this
     * frees only a configuration nobody can reach any more; there is no
     * way to make another one. */
    if (!config || !SDL_AtomicDecRef(&config->ref))
        return;
    FcFontSetDestroy(config->sets[FcSetSystem]);
    FcFontSetDestroy(config->sets[FcSetApplication]);
    SDL_free(config);
}

FcFontSet *
FcConfigGetFonts(FcConfig *config, FcSetName set)
{
    if (!config) config = default_config();
    if (!config || (set != FcSetSystem && set != FcSetApplication))
        return NULL;
    return config->sets[set];
}

FcBool
FcConfigAppFontAddFile(FcConfig *config, const FcChar8 *file)
{
    if (!config) config = default_config();
    if (!config || !file) return FcFalse;
    ns_mutex_lock(&g_lock);
    int added = scan_file_locked((const char *)file, -1, NULL,
                                 config->sets[FcSetApplication]);
    ns_mutex_unlock(&g_lock);
    return added > 0;
}
