/* Weststar SDL Browser — nscore: checksums (GChecksum): SHA-256, SHA-384
 * and SHA-512, the kinds the browser computes. ns_checksum_new returns
 * NULL for the others (MD5, SHA-1), as GLib does for a kind it lacks.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_CHECKSUM_H
#define NS_CHECKSUM_H

#include "ns-base.h"

NS_BEGIN_DECLS

/* GLib's GChecksumType values. */
typedef enum {
    NS_CHECKSUM_MD5,
    NS_CHECKSUM_SHA1,
    NS_CHECKSUM_SHA256,
    NS_CHECKSUM_SHA512,
    NS_CHECKSUM_SHA384
} NsChecksumType;

typedef struct NsChecksum NsChecksum;

ptrdiff_t ns_checksum_type_get_length(NsChecksumType checksum_type);
NsChecksum *ns_checksum_new(NsChecksumType checksum_type);
NsChecksum *ns_checksum_copy(const NsChecksum *checksum);
void ns_checksum_free(NsChecksum *checksum);
void ns_checksum_reset(NsChecksum *checksum);
void ns_checksum_update(NsChecksum *checksum, const unsigned char *data, ptrdiff_t length);
/* Lowercase hex; owned by the checksum, which takes no more data after. */
const char *ns_checksum_get_string(NsChecksum *checksum);
void ns_checksum_get_digest(NsChecksum *checksum, uint8_t *buffer, size_t *digest_len);
char *ns_compute_checksum_for_data(NsChecksumType checksum_type, const unsigned char *data,
                                   size_t length);
char *ns_compute_checksum_for_string(NsChecksumType checksum_type, const char *str,
                                     ptrdiff_t length);

NS_END_DECLS

#endif
