/* Weststar SDL Browser — the one table canPlayType and mediaCapabilities answer from.
 * Maps MIME type and codecs= to probably/maybe/no by the built audio decoders; picks the
 * <source>. There are no video rows: Weststar decodes no video.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "media_types.h"

#include <string.h>

typedef enum {
    NEEDS_MIXER,
    NEEDS_VORBIS_OR_OPUS,
    NEEDS_OPUS,
} ns_media_requirement;

typedef struct {
    const char           *mime;
    ns_media_element      element;
    ns_media_requirement  requires;
    const char *const    *codecs;
} ns_media_type_row;

static const char *const mpeg_audio_codecs[] = {
    "mp3", "mp4a.69", "mp4a.6b", NULL,
};

static const char *const mpeg_system_audio_codecs[] = {
    "mp1v", "mpeg1", "mp2", "mp3", "mp4a.69", "mp4a.6b", NULL,
};

static const char *const ogg_codecs[] = {
    "vorbis", "opus", NULL,
};

static const char *const opus_codecs[] = {
    "opus", NULL,
};

/* An MPEG-1 system stream (video/mpeg) plays in an <audio> element: the
 * mixer decodes its MPEG audio track. */
static const ns_media_type_row media_types[] = {
    { "audio/mpeg",      NS_MEDIA_ELEMENT_AUDIO, NEEDS_MIXER,          mpeg_audio_codecs },
    { "audio/mp3",       NS_MEDIA_ELEMENT_AUDIO, NEEDS_MIXER,          mpeg_audio_codecs },
    { "video/mpeg",      NS_MEDIA_ELEMENT_AUDIO, NEEDS_MIXER,          mpeg_system_audio_codecs },
    { "video/mpg",       NS_MEDIA_ELEMENT_AUDIO, NEEDS_MIXER,          mpeg_system_audio_codecs },
    { "video/x-mpeg",    NS_MEDIA_ELEMENT_AUDIO, NEEDS_MIXER,          mpeg_system_audio_codecs },
    { "video/mpeg-1",    NS_MEDIA_ELEMENT_AUDIO, NEEDS_MIXER,          mpeg_system_audio_codecs },
    { "audio/ogg",       NS_MEDIA_ELEMENT_AUDIO, NEEDS_VORBIS_OR_OPUS, ogg_codecs },
    { "application/ogg", NS_MEDIA_ELEMENT_AUDIO, NEEDS_VORBIS_OR_OPUS, ogg_codecs },
    { "audio/opus",      NS_MEDIA_ELEMENT_AUDIO, NEEDS_OPUS,           opus_codecs },
};

static gboolean
have_mixer(void)
{
#ifdef NS_AUDIO_PLAYBACK
    return TRUE;
#else
    return FALSE;
#endif
}

static gboolean
have_vorbis(void)
{
#ifdef NS_AUDIO_NATIVE_VORBIS
    return TRUE;
#else
    return FALSE;
#endif
}

static gboolean
have_opus(void)
{
#ifdef NS_AUDIO_NATIVE_OPUS
    return TRUE;
#else
    return FALSE;
#endif
}

/* Returns TRUE if this build has the audio support a media type row requires. */
static gboolean
requirement_met(ns_media_requirement requirement)
{
    switch (requirement) {
    case NEEDS_MIXER:          return have_mixer();
    case NEEDS_VORBIS_OR_OPUS: return have_vorbis() || have_opus();
    case NEEDS_OPUS:           return have_opus();
    }
    return FALSE;
}

/* Returns FALSE for vorbis or opus when that decoder is not built in. */
static gboolean
codec_decodable(const char *codec)
{
    if (strcmp(codec, "vorbis") == 0) return have_vorbis();
    if (strcmp(codec, "opus") == 0)   return have_opus();
    return TRUE;
}

/* Returns TRUE if codec (or a dotted profile of it) is listed for the row and decodable. */
static gboolean
codec_listed(const ns_media_type_row *row, const char *codec)
{
    for (int i = 0; row->codecs[i]; i++) {
        const char *known = row->codecs[i];
        size_t n = strlen(known);
        if (strncmp(codec, known, n) == 0 &&
            (codec[n] == '\0' || codec[n] == '.'))
            return codec_decodable(known);
    }
    return FALSE;
}

/* Finds the supported-type row for a MIME essence and media element kind. */
static const ns_media_type_row *
find_row(const char *mime, ns_media_element element)
{
    for (gsize i = 0; i < G_N_ELEMENTS(media_types); i++)
        if (media_types[i].element == element &&
            strcmp(media_types[i].mime, mime) == 0)
            return &media_types[i];
    return NULL;
}

/* Returns the unquoted codecs= parameter from split MIME params; caller frees. */
static char *
codecs_parameter(char **params)
{
    for (int i = 1; params[i]; i++) {
        char *eq = strchr(params[i], '=');
        if (!eq) continue;
        *eq = '\0';
        if (strcmp(g_strstrip(params[i]), "codecs") != 0) continue;
        char *value = g_strstrip(eq + 1);
        size_t n = strlen(value);
        if (n >= 2 && (value[0] == '"' || value[0] == '\'') &&
            value[n - 1] == value[0]) {
            value[n - 1] = '\0';
            value++;
        }
        return g_strdup(value);
    }
    return NULL;
}

/* canPlayType: returns probably, maybe or cannot for a MIME type. */
ns_media_answer
ns_media_type_support(const char *type, ns_media_element element)
{
    if (!type || !*type) return NS_MEDIA_CANNOT;
    char *lower = g_ascii_strdown(type, -1);
    char **params = g_strsplit(lower, ";", -1);
    g_free(lower);
    const char *mime = g_strstrip(params[0]);
    const ns_media_type_row *row = find_row(mime, element);
    if (!row || !requirement_met(row->requires)) {
        g_strfreev(params);
        return NS_MEDIA_CANNOT;
    }
    char *codecs = codecs_parameter(params);
    g_strfreev(params);
    if (!codecs) return NS_MEDIA_MAYBE;
    ns_media_answer answer = NS_MEDIA_PROBABLY;
    char **list = g_strsplit(codecs, ",", -1);
    gboolean any = FALSE;
    for (int i = 0; list[i] && answer != NS_MEDIA_CANNOT; i++) {
        const char *codec = g_strstrip(list[i]);
        if (!*codec) continue;
        any = TRUE;
        if (!codec_listed(row, codec)) answer = NS_MEDIA_CANNOT;
    }
    g_strfreev(list);
    g_free(codecs);
    return any ? answer : NS_MEDIA_MAYBE;
}

const char *
ns_media_answer_string(ns_media_answer answer)
{
    switch (answer) {
    case NS_MEDIA_PROBABLY: return "probably";
    case NS_MEDIA_MAYBE:    return "maybe";
    case NS_MEDIA_CANNOT:   return "";
    }
    return "";
}

/* Picks the media resource: src attribute, else the first playable <source> child. */
const char *
ns_media_select_source(const ns_node *element)
{
    if (!element) return NULL;
    const char *src = ns_element_get_attr(element, "src");
    if (src) return *src ? src : NULL;
    ns_media_element kind = ns_node_is_tag(element, NS_TAG_AUDIO)
        ? NS_MEDIA_ELEMENT_AUDIO : NS_MEDIA_ELEMENT_VIDEO;
    for (const ns_node *c = element->first_child; c; c = c->next_sibling) {
        if (!ns_node_is_tag(c, NS_TAG_SOURCE)) continue;
        const char *csrc = ns_element_get_attr(c, "src");
        if (!csrc || !*csrc) continue;
        const char *type = ns_element_get_attr(c, "type");
        if (type && *type &&
            ns_media_type_support(type, kind) == NS_MEDIA_CANNOT)
            continue;
        return csrc;
    }
    return NULL;
}
