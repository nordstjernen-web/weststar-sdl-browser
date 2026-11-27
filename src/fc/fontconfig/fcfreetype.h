/* Weststar SDL Browser — nsfc: fontconfig's FreeType queries.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NSFC_FCFREETYPE_H
#define NSFC_FCFREETYPE_H

#include <fontconfig/fontconfig.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The pattern of face `id` of a font file (the low 16 bits are the face's
 * index); *count gets the number of faces in the file. */
FcPattern *FcFreeTypeQuery(const FcChar8 *file, unsigned int id,
                           FcBlanks *blanks, int *count);

/* Adds the pattern of every face of a font file (id -1), or of one face,
 * to `set`; returns how many were added. */
unsigned int FcFreeTypeQueryAll(const FcChar8 *file, unsigned int id,
                                FcBlanks *blanks, int *count, FcFontSet *set);

#ifdef __cplusplus
}
#endif

#endif /* NSFC_FCFREETYPE_H */
