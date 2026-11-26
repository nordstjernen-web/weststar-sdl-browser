/* Weststar SDL Browser — which media types the compiled audio decoders can play.
 * Support query behind canPlayType and mediaCapabilities, plus media resource selection.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_MEDIA_TYPES_H
#define NS_MEDIA_TYPES_H

#include <glib.h>

#include "dom.h"

G_BEGIN_DECLS

typedef enum {
    NS_MEDIA_ELEMENT_AUDIO,
    NS_MEDIA_ELEMENT_VIDEO,
} ns_media_element;

typedef enum {
    NS_MEDIA_CANNOT,
    NS_MEDIA_MAYBE,
    NS_MEDIA_PROBABLY,
} ns_media_answer;

/* A <video> element can play nothing: Weststar decodes no video. */
ns_media_answer ns_media_type_support(const char *type,
                                      ns_media_element element);
const char *ns_media_answer_string(ns_media_answer answer);
const char *ns_media_select_source(const ns_node *element);

G_END_DECLS

#endif
