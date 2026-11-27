/* Weststar SDL Browser — guessing the type of a file that came without one.
 * The table and the signatures here take the place of the desktop's MIME
 * database (shared-mime-info, read through GIO; the registry on Windows):
 * the types a browser does something with, the same on every platform.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "mime_guess.h"

#include <string.h>

static const struct {
    const char *ext;
    const char *mime;
} by_extension[] = {
    { "html", "text/html" }, { "htm", "text/html" },
    { "xhtml", "application/xhtml+xml" }, { "xht", "application/xhtml+xml" },
    { "css", "text/css" },
    { "js", "text/javascript" }, { "mjs", "text/javascript" },
    { "json", "application/json" }, { "map", "application/json" },
    { "webmanifest", "application/manifest+json" },
    { "wasm", "application/wasm" },
    { "xml", "text/xml" }, { "xsl", "text/xml" }, { "xslt", "text/xml" },
    { "rss", "application/rss+xml" }, { "atom", "application/atom+xml" },
    { "svg", "image/svg+xml" }, { "svgz", "image/svg+xml" },
    { "mml", "application/mathml+xml" },
    { "txt", "text/plain" }, { "text", "text/plain" }, { "log", "text/plain" },
    { "md", "text/markdown" }, { "csv", "text/csv" },
    { "vtt", "text/vtt" }, { "ics", "text/calendar" },
    { "c", "text/plain" }, { "h", "text/plain" }, { "py", "text/plain" },
    { "sh", "text/plain" }, { "ini", "text/plain" }, { "conf", "text/plain" },
    { "yaml", "text/plain" }, { "yml", "text/plain" }, { "toml", "text/plain" },
    { "diff", "text/plain" }, { "patch", "text/plain" },
    { "png", "image/png" }, { "apng", "image/apng" },
    { "jpg", "image/jpeg" }, { "jpeg", "image/jpeg" }, { "jpe", "image/jpeg" },
    { "jfif", "image/jpeg" },
    { "gif", "image/gif" }, { "webp", "image/webp" }, { "avif", "image/avif" },
    { "bmp", "image/bmp" }, { "ico", "image/x-icon" }, { "cur", "image/x-icon" },
    { "tif", "image/tiff" }, { "tiff", "image/tiff" },
    { "mp3", "audio/mpeg" }, { "ogg", "audio/ogg" }, { "oga", "audio/ogg" },
    { "opus", "audio/ogg" }, { "wav", "audio/wav" }, { "flac", "audio/flac" },
    { "m4a", "audio/mp4" }, { "aac", "audio/aac" }, { "mid", "audio/midi" },
    { "weba", "audio/webm" },
    { "mp4", "video/mp4" }, { "m4v", "video/mp4" }, { "webm", "video/webm" },
    { "ogv", "video/ogg" }, { "mpg", "video/mpeg" }, { "mpeg", "video/mpeg" },
    { "mov", "video/quicktime" }, { "avi", "video/x-msvideo" },
    { "mkv", "video/x-matroska" },
    { "woff", "font/woff" }, { "woff2", "font/woff2" }, { "ttf", "font/ttf" },
    { "otf", "font/otf" }, { "ttc", "font/collection" },
    { "eot", "application/vnd.ms-fontobject" },
    { "pdf", "application/pdf" },
    { "zip", "application/zip" }, { "gz", "application/gzip" },
    { "tgz", "application/gzip" }, { "bz2", "application/x-bzip2" },
    { "xz", "application/x-xz" }, { "zst", "application/zstd" },
    { "tar", "application/x-tar" }, { "7z", "application/x-7z-compressed" },
    { "rar", "application/vnd.rar" }, { "jar", "application/java-archive" },
    { "epub", "application/epub+zip" },
    { "doc", "application/msword" }, { "xls", "application/vnd.ms-excel" },
    { "ppt", "application/vnd.ms-powerpoint" },
    { "docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document" },
    { "xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet" },
    { "pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation" },
    { "odt", "application/vnd.oasis.opendocument.text" },
    { "ods", "application/vnd.oasis.opendocument.spreadsheet" },
    { "odp", "application/vnd.oasis.opendocument.presentation" },
    { "rtf", "application/rtf" },
    { "exe", "application/vnd.microsoft.portable-executable" },
    { "dll", "application/vnd.microsoft.portable-executable" },
    { "deb", "application/vnd.debian.binary-package" },
    { "rpm", "application/x-rpm" }, { "iso", "application/x-iso9660-image" },
    { "bin", "application/octet-stream" },
};

static const char *
guess_by_extension(const char *path)
{
    /* The name ends where a query or a fragment would begin. */
    gsize end = strcspn(path, "?#");
    gsize dot = end;
    while (dot > 0 && path[dot - 1] != '.' && path[dot - 1] != '/' &&
           path[dot - 1] != '\\')
        dot--;
    if (dot == 0 || path[dot - 1] != '.' || dot == end || end - dot > 15)
        return NULL;
    for (gsize i = 0; i < G_N_ELEMENTS(by_extension); i++) {
        if (strlen(by_extension[i].ext) == end - dot &&
            g_ascii_strncasecmp(path + dot, by_extension[i].ext, end - dot) == 0)
            return by_extension[i].mime;
    }
    return NULL;
}

static gboolean
starts(const guchar *data, gsize len, const char *magic, gsize n)
{
    return len >= n && memcmp(data, magic, n) == 0;
}

/* Whether the text after leading blanks (and a byte-order mark) begins
 * with `word`, whatever its case. */
static gboolean
text_starts(const guchar *data, gsize len, const char *word)
{
    gsize i = 0, n = strlen(word);
    if (len >= 3 && data[0] == 0xef && data[1] == 0xbb && data[2] == 0xbf)
        i = 3;
    while (i < len && g_ascii_isspace(data[i]))
        i++;
    return len - i >= n && g_ascii_strncasecmp((const char *)data + i, word, n) == 0;
}

static const char *
guess_by_contents(const guchar *data, gsize len)
{
    if (starts(data, len, "\x89PNG\r\n\x1a\n", 8)) return "image/png";
    if (starts(data, len, "\xff\xd8\xff", 3)) return "image/jpeg";
    if (starts(data, len, "GIF87a", 6) || starts(data, len, "GIF89a", 6))
        return "image/gif";
    if (len >= 12 && starts(data, len, "RIFF", 4)) {
        if (memcmp(data + 8, "WEBP", 4) == 0) return "image/webp";
        if (memcmp(data + 8, "WAVE", 4) == 0) return "audio/wav";
    }
    if (starts(data, len, "BM", 2) && len >= 26) return "image/bmp";
    if (starts(data, len, "%PDF-", 5)) return "application/pdf";
    if (starts(data, len, "\0asm", 4)) return "application/wasm";
    if (starts(data, len, "PK\x03\x04", 4)) return "application/zip";
    if (starts(data, len, "\x1f\x8b", 2)) return "application/gzip";
    if (starts(data, len, "OggS", 4)) return "audio/ogg";
    if (starts(data, len, "ID3", 3)) return "audio/mpeg";
    if (starts(data, len, "fLaC", 4)) return "audio/flac";
    if (starts(data, len, "wOFF", 4)) return "font/woff";
    if (starts(data, len, "wOF2", 4)) return "font/woff2";
    if (len >= 12 && memcmp(data + 4, "ftyp", 4) == 0) {
        if (memcmp(data + 8, "avif", 4) == 0) return "image/avif";
        return "video/mp4";
    }
    if (starts(data, len, "\x1a\x45\xdf\xa3", 4)) return "video/webm";
    if (text_starts(data, len, "<!doctype html") || text_starts(data, len, "<html"))
        return "text/html";
    if (text_starts(data, len, "<svg")) return "image/svg+xml";
    if (text_starts(data, len, "<?xml")) return "text/xml";

    /* Text, if its beginning is valid UTF-8 without control characters
     * (a character cut off at the end of the sample does not count). */
    gsize sample = MIN(len, 4096);
    if (sample == 0)
        return NULL;
    const char *bad = NULL;
    if (!g_utf8_validate((const char *)data, (gssize)sample, &bad) &&
        (gsize)((const guchar *)bad - data) + 4 <= sample)
        return NULL;
    for (gsize i = 0; i < sample; i++)
        if (data[i] < 0x20 && data[i] != '\n' && data[i] != '\r' &&
            data[i] != '\t' && data[i] != '\f')
            return NULL;
    return "text/plain";
}

const char *
ns_mime_guess(const char *path, const guchar *data, gsize len)
{
    const char *mime = path ? guess_by_extension(path) : NULL;
    if (!mime && data && len > 0)
        mime = guess_by_contents(data, len);
    return mime;
}
