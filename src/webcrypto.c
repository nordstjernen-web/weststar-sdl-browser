/* Weststar SDL Browser — SubtleCrypto primitives for js_webcrypto.c.
 * Digest, HMAC, RSA, ECDSA/ECDH, EdDSA/XDH, AES modes, HKDF and PBKDF2 over the platform
 * crypto layer (plat/ns_crypto.h), with the WebCrypto rules kept here.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "webcrypto.h"

#include <string.h>

#include "plat/ns_crypto.h"

/* Sets *err (if unset) to prefix plus the crypto library's last error; clears it, returns NULL. */
static void *
ns_crypto_err(char **err, const char *prefix)
{
    char buf[256];
    gboolean detail = ns_crypto_error_take(buf, sizeof buf);
    if (err && !*err)
        *err = detail ? g_strdup_printf("%s: %s", prefix, buf) : g_strdup(prefix);
    return NULL;
}

static NsPkey *
ns_pkey_of(const ns_crypto_key *k)
{
    return k ? (NsPkey *)k->pkey : NULL;
}

/* Drops a key reference; on last unref wipes raw secret bytes and frees the key pair. */
void
ns_crypto_key_unref(ns_crypto_key *k)
{
    if (!k) return;
    if (--k->refcount > 0) return;
    g_free(k->algo);
    g_free(k->hash);
    g_free(k->curve);
    if (k->raw) {
        ns_crypto_cleanse(k->raw, k->raw_len);
        g_free(k->raw);
    }
    if (k->pkey) ns_pkey_free(k->pkey);
    g_free(k);
}

/* Allocates a CryptoKey record with refcount 1 and copied algorithm strings. */
static ns_crypto_key *
ns_crypto_key_new(ns_ck_type type, const char *algo, const char *hash,
                  const char *curve, int bits, gboolean extractable,
                  guint32 usages)
{
    ns_crypto_key *k = g_new0(ns_crypto_key, 1);
    k->type = type;
    k->algo = g_strdup(algo);
    k->hash = hash ? g_strdup(hash) : NULL;
    k->curve = curve ? g_strdup(curve) : NULL;
    k->bits = bits;
    k->extractable = extractable;
    k->usages = usages;
    k->refcount = 1;
    return k;
}

/* Wraps a key pair in a CryptoKey record that takes ownership of it. */
static ns_crypto_key *
ns_crypto_key_wrap(NsPkey *pkey, ns_ck_type type, const char *algo,
                   const char *hash, const char *curve, gboolean extractable,
                   guint32 usages)
{
    ns_crypto_key *k = ns_crypto_key_new(type, algo, hash, curve,
                                         ns_pkey_bits(pkey), extractable, usages);
    k->pkey = pkey;
    return k;
}

/* Creates a secret key holding a copy of the given raw bytes. */
ns_crypto_key *
ns_crypto_secret_new(const char *algo, const char *hash, const guint8 *data,
                     gsize len, int bits, gboolean extractable, guint32 usages)
{
    ns_crypto_key *k = ns_crypto_key_new(NS_CK_SECRET, algo, hash, NULL, bits,
                                         extractable, usages);
    k->raw = g_malloc(len ? len : 1);
    if (len) memcpy(k->raw, data, len);
    k->raw_len = len;
    return k;
}

/* Returns the asymmetric key family (RSA, EC, Ed25519, ...) of the key pair, or NULL. */
const char *
ns_crypto_key_kind(const ns_crypto_key *k)
{
    if (!k || !k->pkey) return NULL;
    switch (ns_pkey_type(ns_pkey_of(k))) {
    case NS_PKEY_RSA:     return "RSA";
    case NS_PKEY_EC:      return "EC";
    case NS_PKEY_ED25519: return "Ed25519";
    case NS_PKEY_X25519:  return "X25519";
    case NS_PKEY_ED448:   return "Ed448";
    case NS_PKEY_X448:    return "X448";
    }
    return NULL;
}

/* The WebCrypto curve name (P-256/384/521) of an EC key pair, or NULL. */
const char *
ns_crypto_key_curve(const ns_crypto_key *k)
{
    if (!k || !k->pkey) return NULL;
    switch (ns_pkey_curve(ns_pkey_of(k))) {
    case NS_CURVE_P256: return "P-256";
    case NS_CURVE_P384: return "P-384";
    case NS_CURVE_P521: return "P-521";
    default:            return NULL;
    }
}

/* Maps a WebCrypto hash name to a hash: SHA-1/2, and SHA-3 when sha3 is set. */
static gboolean
ns_crypto_hash_of(const char *hash, gboolean sha3, NsHash *out)
{
    static const struct { const char *name; NsHash hash; } hashes[] = {
        { "SHA-1", NS_HASH_SHA1 },
        { "SHA-256", NS_HASH_SHA256 },
        { "SHA-384", NS_HASH_SHA384 },
        { "SHA-512", NS_HASH_SHA512 },
        { "SHA3-256", NS_HASH_SHA3_256 },
        { "SHA3-384", NS_HASH_SHA3_384 },
        { "SHA3-512", NS_HASH_SHA3_512 },
    };
    if (!hash) return FALSE;
    for (gsize i = 0; i < G_N_ELEMENTS(hashes); i++) {
        if (g_ascii_strcasecmp(hash, hashes[i].name)) continue;
        if (!sha3 && i >= 4) return FALSE;
        if (!ns_hash_available(hashes[i].hash)) return FALSE;
        *out = hashes[i].hash;
        return TRUE;
    }
    return FALSE;
}

/* Computes a digest of data with the named hash; caller frees, NULL if unsupported. */
guint8 *
ns_crypto_digest(const char *hash, const guint8 *data, gsize len, gsize *out_len)
{
    NsHash h;
    if (!ns_crypto_hash_of(hash, TRUE, &h)) return NULL;
    guint8 *out = g_malloc(ns_hash_size(h));
    if (!ns_hash(h, data, len, out)) {
        g_free(out);
        return NULL;
    }
    if (out_len) *out_len = ns_hash_size(h);
    return out;
}

/* Maps a WebCrypto named curve to the platform curve. */
static NsCurve
ns_crypto_curve_of(const char *curve)
{
    if (!curve) return NS_CURVE_NONE;
    if (!g_ascii_strcasecmp(curve, "P-256")) return NS_CURVE_P256;
    if (!g_ascii_strcasecmp(curve, "P-384")) return NS_CURVE_P384;
    if (!g_ascii_strcasecmp(curve, "P-521")) return NS_CURVE_P521;
    return NS_CURVE_NONE;
}

/* Byte length of a curve's field coordinates (32, 48 or 66), 0 if unknown. */
static int
ns_crypto_curve_order_bytes(const char *curve)
{
    switch (ns_crypto_curve_of(curve)) {
    case NS_CURVE_P256: return 32;
    case NS_CURVE_P384: return 48;
    case NS_CURVE_P521: return 66;
    default:            return 0;
    }
}

/* Maps Ed25519/X25519/Ed448/X448 to the key type; FALSE for other names. */
static gboolean
ns_crypto_okp_type(const char *algo, NsPkeyType *out)
{
    static const struct { const char *name; NsPkeyType type; } okp[] = {
        { "Ed25519", NS_PKEY_ED25519 },
        { "X25519", NS_PKEY_X25519 },
        { "Ed448", NS_PKEY_ED448 },
        { "X448", NS_PKEY_X448 },
    };
    if (!algo) return FALSE;
    for (gsize i = 0; i < G_N_ELEMENTS(okp); i++) {
        if (!g_ascii_strcasecmp(algo, okp[i].name)) {
            if (out) *out = okp[i].type;
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean
ns_crypto_is_eddsa(const char *algo)
{
    return !g_strcmp0(algo, "Ed25519") || !g_strcmp0(algo, "Ed448");
}

static gboolean
ns_crypto_is_xdh(const char *algo)
{
    return !g_strcmp0(algo, "X25519") || !g_strcmp0(algo, "X448");
}

/* True if the crypto library's Ed448 signer accepts a context-string parameter. */
gboolean
ns_crypto_eddsa_context_supported(void)
{
    return ns_pkey_eddsa_context_supported();
}

/* False for Ed25519/Ed448/X25519/X448 or a SHA-3 hash the crypto library lacks. */
gboolean
ns_crypto_algorithm_available(const char *name)
{
    NsPkeyType type;
    NsHash h;
    if (ns_crypto_okp_type(name, &type)) return ns_pkey_type_available(type);
    if (name && !g_ascii_strncasecmp(name, "SHA3-", 5))
        return ns_crypto_hash_of(name, TRUE, &h);
    return TRUE;
}

/* Generates a random symmetric key of length_bits (multiple of 8, max 4096). */
ns_crypto_key *
ns_crypto_generate_secret(const char *algo, const char *hash, int length_bits,
                          gboolean extractable, guint32 usages, char **err)
{
    if (length_bits <= 0 || length_bits % 8 != 0 || length_bits > 4096) {
        if (err) *err = g_strdup("OperationError: invalid key length");
        return NULL;
    }
    gsize len = (gsize)length_bits / 8;
    guint8 *buf = g_malloc(len);
    if (!ns_crypto_random(buf, len)) {
        ns_crypto_cleanse(buf, len);
        g_free(buf);
        return ns_crypto_err(err, "OperationError: RNG failure");
    }
    ns_crypto_key *k = ns_crypto_key_new(NS_CK_SECRET, algo, hash, NULL,
                                         length_bits, extractable, usages);
    k->raw = buf;
    k->raw_len = len;
    return k;
}

/* Generates an OKP, EC or RSA key pair; the public half is always extractable. */
gboolean
ns_crypto_generate_keypair(const char *algo, const char *hash, const char *curve,
                           int modulus_bits, guint32 pubexp, gboolean extractable,
                           guint32 usages, ns_crypto_key **pub, ns_crypto_key **priv,
                           char **err)
{
    *pub = NULL;
    *priv = NULL;
    NsPkey *pkey = NULL;
    gboolean is_ec = !g_ascii_strcasecmp(algo, "ECDSA") ||
                     !g_ascii_strcasecmp(algo, "ECDH");
    NsPkeyType okp;

    if (ns_crypto_okp_type(algo, &okp)) {
        if (!ns_pkey_type_available(okp)) {
            if (err) *err = g_strdup("NotSupportedError: algorithm");
            return FALSE;
        }
        pkey = ns_pkey_generate_okp(okp);
        if (!pkey) return ns_crypto_err(err, "OperationError: keygen"), FALSE;
    } else if (is_ec) {
        NsCurve c = ns_crypto_curve_of(curve);
        if (c == NS_CURVE_NONE) { if (err) *err = g_strdup("NotSupportedError: curve"); return FALSE; }
        pkey = ns_pkey_generate_ec(c);
        if (!pkey) return ns_crypto_err(err, "OperationError: EC keygen"), FALSE;
    } else {
        if (modulus_bits < 256 || modulus_bits > 16384) {
            if (err) *err = g_strdup("OperationError: invalid modulus length");
            return FALSE;
        }
        pkey = ns_pkey_generate_rsa(modulus_bits, pubexp);
        if (!pkey) return ns_crypto_err(err, "OperationError: RSA keygen"), FALSE;
    }

    NsPkey *dup = ns_pkey_dup(pkey);
    ns_crypto_key *pk = ns_crypto_key_wrap(pkey, NS_CK_PUBLIC, algo, hash, curve,
                                           TRUE, usages);
    if (!dup) {
        ns_crypto_key_unref(pk);
        return ns_crypto_err(err, "OperationError: key duplication"), FALSE;
    }
    *pub = pk;
    *priv = ns_crypto_key_wrap(dup, NS_CK_PRIVATE, algo, hash, curve,
                               extractable, usages);
    return TRUE;
}

/* importKey for raw secret, raw EC/OKP public, SPKI and PKCS#8 formats. */
ns_crypto_key *
ns_crypto_import_raw(const char *format, const guint8 *data, gsize len,
                     const char *algo, const char *hash, const char *curve,
                     gboolean extractable, guint32 usages, char **err)
{
    gboolean symmetric = !g_ascii_strcasecmp(algo, "HMAC") ||
                         !g_ascii_strncasecmp(algo, "AES", 3) ||
                         !g_ascii_strcasecmp(algo, "PBKDF2") ||
                         !g_ascii_strcasecmp(algo, "HKDF");
    NsPkeyType okp;

    if (!g_strcmp0(format, "raw") && symmetric) {
        if (!g_ascii_strncasecmp(algo, "AES", 3) &&
            len != 16 && len != 24 && len != 32) {
            if (err) *err = g_strdup("DataError: invalid AES key length");
            return NULL;
        }
        ns_crypto_key *k = ns_crypto_key_new(NS_CK_SECRET, algo, hash, NULL,
                                             (int)len * 8, extractable, usages);
        k->raw = g_memdup2(data, len);
        k->raw_len = len;
        return k;
    }

    if (!g_strcmp0(format, "raw") &&
        (!g_ascii_strcasecmp(algo, "ECDSA") || !g_ascii_strcasecmp(algo, "ECDH"))) {
        NsCurve c = ns_crypto_curve_of(curve);
        if (c == NS_CURVE_NONE) { if (err) *err = g_strdup("NotSupportedError: curve"); return NULL; }
        NsPkey *pkey = ns_pkey_from_ec_point(c, data, len);
        if (!pkey) return ns_crypto_err(err, "DataError: EC raw import");
        return ns_crypto_key_wrap(pkey, NS_CK_PUBLIC, algo, hash, curve,
                                  extractable, usages);
    }

    if (!g_strcmp0(format, "raw") && ns_crypto_okp_type(algo, &okp)) {
        NsPkey *pkey = ns_pkey_type_available(okp)
                           ? ns_pkey_from_okp(okp, data, len, FALSE) : NULL;
        if (!pkey) return ns_crypto_err(err, "DataError: raw import");
        return ns_crypto_key_wrap(pkey, NS_CK_PUBLIC, algo, hash, NULL,
                                  extractable, usages);
    }

    NsPkey *pkey = !g_strcmp0(format, "spki") ? ns_pkey_from_spki(data, len)
                 : !g_strcmp0(format, "pkcs8") ? ns_pkey_from_pkcs8(data, len)
                 : NULL;
    if (!pkey) return ns_crypto_err(err, "DataError: key import");
    ns_ck_type t = !g_strcmp0(format, "pkcs8") ? NS_CK_PRIVATE : NS_CK_PUBLIC;
    return ns_crypto_key_wrap(pkey, t, algo, hash, curve, extractable, usages);
}

/* Builds an RSA public or private key from JWK components (CRT values optional). */
ns_crypto_key *
ns_crypto_import_rsa_jwk(const guint8 *n, gsize n_len, const guint8 *e, gsize e_len,
                         const guint8 *d, gsize d_len, const guint8 *p, gsize p_len,
                         const guint8 *q, gsize q_len, const guint8 *dp, gsize dp_len,
                         const guint8 *dq, gsize dq_len, const guint8 *qi, gsize qi_len,
                         const char *algo, const char *hash, gboolean extractable,
                         guint32 usages, char **err)
{
    gboolean private = d && d_len;
    const NsBytes parts[NS_RSA_PARTS] = {
        [NS_RSA_N] = { n, n_len },   [NS_RSA_E] = { e, e_len },
        [NS_RSA_D] = { d, d_len },   [NS_RSA_P] = { p, p_len },
        [NS_RSA_Q] = { q, q_len },   [NS_RSA_DP] = { dp, dp_len },
        [NS_RSA_DQ] = { dq, dq_len }, [NS_RSA_QI] = { qi, qi_len },
    };
    NsPkey *pkey = ns_pkey_from_rsa(parts);
    if (!pkey) return ns_crypto_err(err, "DataError: RSA JWK import");
    return ns_crypto_key_wrap(pkey, private ? NS_CK_PRIVATE : NS_CK_PUBLIC, algo,
                              hash, NULL, extractable, usages);
}

/* Builds an EC key from JWK x/y (and d), rejecting oversized or inconsistent pairs. */
ns_crypto_key *
ns_crypto_import_ec_jwk(const char *curve, const guint8 *x, gsize x_len,
                        const guint8 *y, gsize y_len, const guint8 *d, gsize d_len,
                        const char *algo, gboolean extractable, guint32 usages,
                        char **err)
{
    NsCurve c = ns_crypto_curve_of(curve);
    int order = ns_crypto_curve_order_bytes(curve);
    if (c == NS_CURVE_NONE || !order || !x || !y) {
        if (err) *err = g_strdup("DataError: EC JWK");
        return NULL;
    }
    if (x_len > (gsize)order || y_len > (gsize)order) {
        if (err) *err = g_strdup("DataError: EC JWK coordinate too large");
        return NULL;
    }
    gsize plen = 1 + 2 * (gsize)order;
    guint8 *point = g_malloc0(plen);
    point[0] = 0x04;
    memcpy(point + 1 + order - x_len, x, x_len);
    memcpy(point + 1 + 2 * order - y_len, y, y_len);

    gboolean private = d && d_len;
    NsPkey *pkey = private ? ns_pkey_from_ec_private(c, point, plen, d, d_len)
                           : ns_pkey_from_ec_point(c, point, plen);
    g_free(point);
    if (!pkey) return ns_crypto_err(err, "DataError: EC JWK import");
    return ns_crypto_key_wrap(pkey, private ? NS_CK_PRIVATE : NS_CK_PUBLIC, algo,
                              NULL, curve, extractable, usages);
}

/* Builds an OKP key from JWK d (private) or x (public). */
ns_crypto_key *
ns_crypto_import_okp_jwk(const char *curve, const guint8 *x, gsize x_len,
                         const guint8 *d, gsize d_len, const char *algo,
                         gboolean extractable, guint32 usages, char **err)
{
    NsPkeyType okp;
    gboolean private = d && d_len;
    if (!ns_crypto_okp_type(curve, &okp) || (!private && (!x || !x_len))) {
        if (err) *err = g_strdup("DataError: OKP JWK");
        return NULL;
    }
    NsPkey *pkey = !ns_pkey_type_available(okp) ? NULL
                 : private ? ns_pkey_from_okp(okp, d, d_len, TRUE)
                           : ns_pkey_from_okp(okp, x, x_len, FALSE);
    if (!pkey) return ns_crypto_err(err, "DataError: OKP JWK import");
    return ns_crypto_key_wrap(pkey, private ? NS_CK_PRIVATE : NS_CK_PUBLIC, algo,
                              NULL, NULL, extractable, usages);
}

/* Exports an OKP key's raw x and, for private keys, d; caller frees. */
gboolean
ns_crypto_export_okp_jwk(const ns_crypto_key *k, guint8 **x, gsize *x_len,
                         guint8 **d, gsize *d_len, char **err)
{
    *x = *d = NULL;
    *x_len = *d_len = 0;
    if (!k->pkey || !ns_crypto_okp_type(k->algo, NULL)) {
        if (err) *err = g_strdup("OperationError: OKP export");
        return FALSE;
    }
    size_t n = 0;
    *x = ns_pkey_okp_public(ns_pkey_of(k), &n);
    if (!*x) {
        ns_crypto_err(err, "OperationError: OKP export");
        return FALSE;
    }
    *x_len = n;
    if (k->type == NS_CK_PRIVATE) {
        size_t m = 0;
        guint8 *priv = ns_pkey_okp_private(ns_pkey_of(k), &m);
        if (!priv) {
            g_clear_pointer(x, g_free);
            *x_len = 0;
            ns_crypto_err(err, "OperationError: OKP export");
            return FALSE;
        }
        *d = priv;
        *d_len = m;
    }
    return TRUE;
}

/* exportKey for raw, spki and pkcs8 formats; returns new bytes, caller frees. */
guint8 *
ns_crypto_export_raw(const char *format, const ns_crypto_key *k, gsize *out_len,
                     char **err)
{
    if (!g_strcmp0(format, "raw") && k->raw) {
        *out_len = k->raw_len;
        return g_memdup2(k->raw, k->raw_len);
    }
    if (!k->pkey) { if (err) *err = g_strdup("NotSupportedError: export"); return NULL; }

    size_t n = 0;
    guint8 *out;
    if (!g_strcmp0(format, "raw") && ns_crypto_okp_type(k->algo, NULL)) {
        out = ns_pkey_okp_public(ns_pkey_of(k), &n);
        if (!out) return ns_crypto_err(err, "OperationError: raw export");
    } else if (!g_strcmp0(format, "raw") &&
               (!g_strcmp0(k->algo, "ECDSA") || !g_strcmp0(k->algo, "ECDH"))) {
        out = ns_pkey_ec_point(ns_pkey_of(k), &n);
        if (!out) return ns_crypto_err(err, "OperationError: raw export");
    } else if (!g_strcmp0(format, "spki")) {
        out = ns_pkey_to_spki(ns_pkey_of(k), &n);
        if (!out) return ns_crypto_err(err, "OperationError: spki export");
    } else if (!g_strcmp0(format, "pkcs8")) {
        out = ns_pkey_to_pkcs8(ns_pkey_of(k), &n);
        if (!out) return ns_crypto_err(err, "OperationError: pkcs8 export");
    } else {
        if (err) *err = g_strdup("NotSupportedError: export format");
        return NULL;
    }
    *out_len = n;
    return out;
}

/* One RSA component as minimal big-endian bytes; NULL if the key lacks it. */
static guint8 *
ns_crypto_rsa_part(const ns_crypto_key *k, NsRsaPart part, gsize *len)
{
    size_t n = 0;
    guint8 *out = ns_pkey_rsa_part(ns_pkey_of(k), part, &n);
    if (out) *len = n;
    return out;
}

/* Returns the RSA public exponent as big-endian bytes; caller frees. */
guint8 *
ns_crypto_rsa_exponent(const ns_crypto_key *k, gsize *len)
{
    *len = 0;
    return k && k->pkey ? ns_crypto_rsa_part(k, NS_RSA_E, len) : NULL;
}

/* Exports RSA JWK n/e and private CRT components; caller frees each buffer. */
gboolean
ns_crypto_export_rsa_jwk(const ns_crypto_key *k, guint8 **n, gsize *n_len,
                         guint8 **e, gsize *e_len, guint8 **d, gsize *d_len,
                         guint8 **p, gsize *p_len, guint8 **q, gsize *q_len,
                         guint8 **dp, gsize *dp_len, guint8 **dq, gsize *dq_len,
                         guint8 **qi, gsize *qi_len, char **err)
{
    if (!k->pkey) { if (err) *err = g_strdup("export"); return FALSE; }
    *n = ns_crypto_rsa_part(k, NS_RSA_N, n_len);
    *e = ns_crypto_rsa_part(k, NS_RSA_E, e_len);
    *d = *p = *q = *dp = *dq = *qi = NULL;
    *d_len = *p_len = *q_len = *dp_len = *dq_len = *qi_len = 0;
    if (k->type == NS_CK_PRIVATE) {
        *d = ns_crypto_rsa_part(k, NS_RSA_D, d_len);
        *p = ns_crypto_rsa_part(k, NS_RSA_P, p_len);
        *q = ns_crypto_rsa_part(k, NS_RSA_Q, q_len);
        *dp = ns_crypto_rsa_part(k, NS_RSA_DP, dp_len);
        *dq = ns_crypto_rsa_part(k, NS_RSA_DQ, dq_len);
        *qi = ns_crypto_rsa_part(k, NS_RSA_QI, qi_len);
    }
    if (*n && *e)
        return TRUE;
    g_clear_pointer(n, g_free);
    g_clear_pointer(e, g_free);
    g_clear_pointer(d, g_free);
    g_clear_pointer(p, g_free);
    g_clear_pointer(q, g_free);
    g_clear_pointer(dp, g_free);
    g_clear_pointer(dq, g_free);
    g_clear_pointer(qi, g_free);
    if (err) *err = g_strdup("export");
    return FALSE;
}

/* Exports EC JWK x/y (and zero-padded d for private keys); caller frees. */
gboolean
ns_crypto_export_ec_jwk(const ns_crypto_key *k, guint8 **x, gsize *x_len,
                        guint8 **y, gsize *y_len, guint8 **d, gsize *d_len,
                        char **err)
{
    if (!k->pkey) { if (err) *err = g_strdup("export"); return FALSE; }
    int order = ns_crypto_curve_order_bytes(k->curve);
    if (order <= 0) { if (err) *err = g_strdup("OperationError: EC export"); return FALSE; }
    size_t enc_len = 0;
    guint8 *enc = ns_pkey_ec_point(ns_pkey_of(k), &enc_len);
    *x = *y = *d = NULL;
    *x_len = *y_len = *d_len = 0;
    if (enc && enc_len == 1 + 2 * (gsize)order && enc[0] == 0x04) {
        *x = g_memdup2(enc + 1, order);
        *y = g_memdup2(enc + 1 + order, order);
        *x_len = *y_len = order;
    }
    g_free(enc);
    if (!*x) { if (err) *err = g_strdup("OperationError: EC export"); return FALSE; }
    if (k->type == NS_CK_PRIVATE) {
        size_t raw_len = 0;
        guint8 *raw = ns_pkey_ec_private(ns_pkey_of(k), &raw_len);
        if (!raw) {
            g_free(*x); g_free(*y);
            *x = *y = NULL; *x_len = *y_len = 0;
            if (err) *err = g_strdup("OperationError: EC export");
            return FALSE;
        }
        *d_len = raw_len;
        if (*d_len < (gsize)order) {
            guint8 *pad = g_malloc0(order);
            memcpy(pad + order - *d_len, raw, *d_len);
            ns_crypto_cleanse(raw, raw_len);
            g_free(raw);
            raw = pad;
            *d_len = order;
        }
        *d = raw;
    }
    return TRUE;
}

/* Computes HMAC over data with the key's hash; caller frees the MAC. */
static guint8 *
ns_crypto_hmac(const ns_crypto_key *k, const guint8 *data, gsize len,
               gsize *out_len, char **err)
{
    NsHash h;
    if (!ns_crypto_hash_of(k->hash, FALSE, &h)) { if (err) *err = g_strdup("NotSupportedError: hash"); return NULL; }
    guint8 *out = g_malloc(ns_hash_size(h));
    if (!ns_hmac(h, k->raw, k->raw_len, data, len, out)) {
        g_free(out);
        return ns_crypto_err(err, "OperationError: HMAC");
    }
    *out_len = ns_hash_size(h);
    return out;
}

/* Fills the signature parameters for an RSA, ECDSA or EdDSA key; FALSE without a hash. */
static gboolean
ns_crypto_sig_params(const ns_crypto_key *k, const ns_crypto_params *p,
                     NsSigParams *sp)
{
    memset(sp, 0, sizeof *sp);
    if (ns_crypto_is_eddsa(k->algo)) {
        sp->scheme = NS_SIG_EDDSA;
        sp->context = p->context;
        sp->context_len = p->context_len;
        return TRUE;
    }
    if (!ns_crypto_hash_of(p->sign_hash ? p->sign_hash : k->hash, TRUE, &sp->hash))
        return FALSE;
    if (!g_strcmp0(k->algo, "ECDSA")) {
        sp->scheme = NS_SIG_ECDSA;
    } else if (!g_strcmp0(k->algo, "RSA-PSS")) {
        sp->scheme = NS_SIG_RSA_PSS;
        sp->pss_salt_len = p->pss_salt_len < 0 ? ns_hash_size(sp->hash)
                                               : (size_t)p->pss_salt_len;
    } else {
        sp->scheme = NS_SIG_RSA_PKCS1;
    }
    return TRUE;
}

/* SubtleCrypto sign for HMAC, EdDSA, RSA and ECDSA (raw r||s output); caller frees. */
guint8 *
ns_crypto_sign(const ns_crypto_key *k, const ns_crypto_params *p, const guint8 *data,
               gsize len, gsize *out_len, char **err)
{
    if (!g_strcmp0(k->algo, "HMAC"))
        return ns_crypto_hmac(k, data, len, out_len, err);

    NsSigParams sp;
    if (!ns_crypto_sig_params(k, p, &sp) || !k->pkey) {
        if (err) *err = g_strdup("NotSupportedError: sign");
        return NULL;
    }
    if (sp.scheme == NS_SIG_ECDSA && ns_crypto_curve_order_bytes(k->curve) <= 0)
        return ns_crypto_err(err, "OperationError: unsupported ECDSA curve");
    size_t n = 0;
    guint8 *out = ns_pkey_sign(ns_pkey_of(k), &sp, data, len, &n);
    if (!out) return ns_crypto_err(err, "OperationError: sign");
    *out_len = n;
    return out;
}

/* True if a 32-byte Ed25519 encoding is a known small-order point. */
static gboolean
ns_crypto_ed25519_small_order(const guint8 *point)
{
    static const guint8 blocked[][32] = {
        { 0 },
        { 0x01 },
        { 0x26, 0xe8, 0x95, 0x8f, 0xc2, 0xb2, 0x27, 0xb0, 0x45, 0xc3, 0xf4,
          0x89, 0xf2, 0xef, 0x98, 0xf0, 0xd5, 0xdf, 0xac, 0x05, 0xd3, 0xc6,
          0x33, 0x39, 0xb1, 0x38, 0x02, 0x88, 0x6d, 0x53, 0xfc, 0x05 },
        { 0xc7, 0x17, 0x6a, 0x70, 0x3d, 0x4d, 0xd8, 0x4f, 0xba, 0x3c, 0x0b,
          0x76, 0x0d, 0x10, 0x67, 0x0f, 0x2a, 0x20, 0x53, 0xfa, 0x2c, 0x39,
          0xcc, 0xc6, 0x4e, 0xc7, 0xfd, 0x77, 0x92, 0xac, 0x03, 0x7a },
        { 0xec, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
          0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
          0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f },
        { 0xed, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
          0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
          0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f },
        { 0xee, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
          0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
          0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(blocked); i++) {
        if (memcmp(point, blocked[i], 31) == 0 &&
            (point[31] & 0x7f) == blocked[i][31])
            return TRUE;
    }
    return FALSE;
}

/* True if a 57-byte Ed448 encoding is a known small-order point. */
static gboolean
ns_crypto_ed448_small_order(const guint8 *point)
{
    static const struct {
        guint8 low, low_fill, high, high_fill;
    } blocked[] = {
        { 0x00, 0x00, 0x00, 0x00 },
        { 0x01, 0x00, 0x00, 0x00 },
        { 0xfe, 0xff, 0xfe, 0xff },
        { 0xff, 0xff, 0xfe, 0xff },
        { 0x00, 0x00, 0xff, 0xff },
    };
    if (point[56] & 0x7f) return FALSE;
    for (gsize i = 0; i < G_N_ELEMENTS(blocked); i++) {
        guint8 y[56];
        y[0] = blocked[i].low;
        memset(y + 1, blocked[i].low_fill, 27);
        y[28] = blocked[i].high;
        memset(y + 29, blocked[i].high_fill, 27);
        if (memcmp(point, y, sizeof y) == 0) return TRUE;
    }
    return FALSE;
}

/* True if signature length is wrong or the public key or R is a small-order point. */
static gboolean
ns_crypto_eddsa_weak(const ns_crypto_key *k, const guint8 *sig, gsize sig_len)
{
    gboolean ed448 = !g_strcmp0(k->algo, "Ed448");
    gsize point_len = ed448 ? 57 : 32;
    gboolean (*small_order)(const guint8 *) =
        ed448 ? ns_crypto_ed448_small_order : ns_crypto_ed25519_small_order;
    size_t n = 0;
    guint8 *pub = sig_len == 2 * point_len ? ns_pkey_okp_public(ns_pkey_of(k), &n)
                                           : NULL;
    gboolean weak = !pub || n != point_len || small_order(pub) || small_order(sig);
    g_free(pub);
    ns_crypto_error_clear();
    return weak;
}

/* SubtleCrypto verify; returns 1 valid, 0 invalid, -1 on error (constant-time HMAC). */
int
ns_crypto_verify(const ns_crypto_key *k, const ns_crypto_params *p, const guint8 *sig,
                 gsize sig_len, const guint8 *data, gsize len, char **err)
{
    if (!g_strcmp0(k->algo, "HMAC")) {
        gsize mlen = 0;
        guint8 *mac = ns_crypto_hmac(k, data, len, &mlen, err);
        if (!mac) return -1;
        int ok = (mlen == sig_len) && ns_crypto_memeq(mac, sig, mlen);
        ns_crypto_cleanse(mac, mlen);
        g_free(mac);
        return ok ? 1 : 0;
    }

    NsSigParams sp;
    if (!ns_crypto_sig_params(k, p, &sp) || !k->pkey) {
        if (err) *err = g_strdup("NotSupportedError: verify");
        return -1;
    }
    if (sp.scheme == NS_SIG_EDDSA && ns_crypto_eddsa_weak(k, sig, sig_len))
        return 0;
    if (sp.scheme == NS_SIG_ECDSA) {
        int order = ns_crypto_curve_order_bytes(k->curve);
        if (order <= 0) { if (err) *err = g_strdup("OperationError: unsupported ECDSA curve"); return -1; }
        if (sig_len != (gsize)order * 2) { if (err) *err = g_strdup("OperationError: ECDSA decode"); return -1; }
    }
    int rc = ns_pkey_verify(ns_pkey_of(k), &sp, sig, sig_len, data, len);
    if (rc < 0) ns_crypto_err(err, "OperationError: verify");
    return rc;
}

static gboolean
ns_crypto_is_aes(const char *algo)
{
    return algo && !g_ascii_strncasecmp(algo, "AES-", 4);
}

/* Counts blocks until the low counter_bits of the counter block wrap to zero. */
static gboolean
ctr_blocks_before_wrap(const guint8 *counter, int counter_bits, guint64 *blocks)
{
    guint64 low = 0;
    for (int i = 8; i < 16; i++) low = (low << 8) | counter[i];
    if (counter_bits >= 64) {
        for (int bit = 64; bit < counter_bits; bit++)
            if (!((counter[15 - bit / 8] >> (bit % 8)) & 1)) return FALSE;
        *blocks = low == 0 ? G_MAXUINT64 : ~low + 1;
        return TRUE;
    }
    guint64 mask = (G_GUINT64_CONSTANT(1) << counter_bits) - 1;
    *blocks = mask - (low & mask) + 1;
    return TRUE;
}

/* AES-CTR with a counter_bits-wide counter, wrapping it as WebCrypto requires. */
static guint8 *
ns_crypto_aes_ctr(const ns_crypto_key *k, const ns_crypto_params *p,
                  const guint8 *data, gsize len, gsize *out_len, char **err)
{
    int bits = p->counter_bits;
    if (bits < 1 || bits > 128)
        return ns_crypto_err(err, "OperationError: invalid AES-CTR length");
    guint64 blocks = (len + 15) / 16;
    guint64 before_wrap = G_MAXUINT64;
    gboolean wraps = bits < 128 &&
        ctr_blocks_before_wrap(p->iv, bits, &before_wrap) &&
        blocks > before_wrap;
    if (wraps && bits < 64 && blocks > (G_GUINT64_CONSTANT(1) << bits))
        return ns_crypto_err(err, "OperationError: AES-CTR counter would repeat");
    guint8 *out = g_malloc(len + 16);
    gsize head = wraps ? (gsize)before_wrap * 16 : len;
    gboolean ok = ns_aes_ctr(k->raw, k->raw_len, p->iv, data, head, out);
    if (ok && wraps) {
        guint8 restart[16];
        memcpy(restart, p->iv, 16);
        for (int bit = 0; bit < bits; bit++)
            restart[15 - bit / 8] &= (guint8)~(1u << (bit % 8));
        ok = ns_aes_ctr(k->raw, k->raw_len, restart, data + head, len - head,
                        out + head);
    }
    if (!ok) {
        g_free(out);
        return ns_crypto_err(err, "OperationError: AES-CTR");
    }
    *out_len = len;
    return out;
}

/* AES-GCM/CBC/CTR/KW encrypt or decrypt with WebCrypto parameter validation. */
static guint8 *
ns_crypto_aes(const ns_crypto_key *k, const ns_crypto_params *p, const guint8 *data,
              gsize len, gboolean enc, gsize *out_len, char **err)
{
    gboolean gcm = !g_strcmp0(k->algo, "AES-GCM");
    gboolean kw = !g_strcmp0(k->algo, "AES-KW");
    gboolean cbc = !g_strcmp0(k->algo, "AES-CBC");
    gboolean ctr = !g_strcmp0(k->algo, "AES-CTR");
    if ((!gcm && !kw && !cbc && !ctr) || !k->raw ||
        (k->bits != 128 && k->bits != 192 && k->bits != 256) ||
        k->raw_len * 8 != (gsize)k->bits) {
        if (err) *err = g_strdup("NotSupportedError: AES");
        return NULL;
    }
    if (gcm) {
        if (!p->iv || p->iv_len == 0) {
            if (err) *err = g_strdup("OperationError: invalid AES-GCM IV");
            return NULL;
        }
    } else if (kw) {
        if (len % 8 != 0 || len < (gsize)(enc ? 16 : 24)) {
            if (err) *err = g_strdup("OperationError: invalid AES-KW length");
            return NULL;
        }
    } else {
        if (!p->iv || p->iv_len != 16) {
            if (err) *err = g_strdup("OperationError: invalid AES IV length");
            return NULL;
        }
        if (ctr)
            return ns_crypto_aes_ctr(k, p, data, len, out_len, err);
    }
    int tag_bits = gcm ? (p->tag_bits > 0 ? p->tag_bits : 128) : 0;
    gsize tag_len = (gsize)tag_bits / 8;
    if (gcm) {
        switch (tag_bits) {
        case 32: case 64: case 96: case 104: case 112: case 120: case 128:
            break;
        default:
            if (err) *err = g_strdup("OperationError: invalid AES-GCM tag length");
            return NULL;
        }
    }

    guint8 *out = g_malloc(len + 32 + tag_len);
    gsize produced = 0;
    gboolean ok;
    if (gcm && enc) {
        ok = ns_aes_gcm_encrypt(k->raw, k->raw_len, p->iv, p->iv_len, p->aad,
                                p->aad_len, data, len, out, out + len, tag_len);
        produced = len + tag_len;
    } else if (gcm) {
        ok = len >= tag_len &&
             ns_aes_gcm_decrypt(k->raw, k->raw_len, p->iv, p->iv_len, p->aad,
                                p->aad_len, data, len - tag_len,
                                data + len - tag_len, tag_len, out);
        produced = len >= tag_len ? len - tag_len : 0;
    } else if (kw) {
        ok = enc ? ns_aes_kw_wrap(k->raw, k->raw_len, data, len, out)
                 : ns_aes_kw_unwrap(k->raw, k->raw_len, data, len, out);
        produced = enc ? len + 8 : len - 8;
    } else {
        ok = ns_aes_cbc(enc, k->raw, k->raw_len, p->iv, data, len, out, &produced);
    }
    if (!ok) {
        ns_crypto_cleanse(out, len + 32 + tag_len);
        g_free(out);
        return ns_crypto_err(err, enc ? "OperationError: encrypt"
                                      : "OperationError: decrypt");
    }
    *out_len = produced;
    return out;
}

/* RSA-OAEP encrypt or decrypt using the key's hash for OAEP and MGF1, with label. */
static guint8 *
ns_crypto_rsa_oaep(const ns_crypto_key *k, const ns_crypto_params *p,
                   const guint8 *data, gsize len, gboolean enc, gsize *out_len,
                   char **err)
{
    NsHash h;
    if (!ns_crypto_hash_of(k->hash, TRUE, &h) || !k->pkey) {
        if (err) *err = g_strdup("NotSupportedError: RSA-OAEP");
        return NULL;
    }
    size_t n = 0;
    guint8 *out = ns_pkey_rsa_oaep(ns_pkey_of(k), enc, h, p->label, p->label_len,
                                   data, len, &n);
    if (!out) return ns_crypto_err(err, enc ? "OperationError: encrypt"
                                            : "OperationError: decrypt");
    *out_len = n;
    return out;
}

/* SubtleCrypto encrypt dispatch for AES and RSA-OAEP; caller frees. */
guint8 *
ns_crypto_encrypt(const ns_crypto_key *k, const ns_crypto_params *p,
                  const guint8 *data, gsize len, gsize *out_len, char **err)
{
    if (ns_crypto_is_aes(k->algo))
        return ns_crypto_aes(k, p, data, len, TRUE, out_len, err);
    if (!g_strcmp0(k->algo, "RSA-OAEP"))
        return ns_crypto_rsa_oaep(k, p, data, len, TRUE, out_len, err);
    if (err) *err = g_strdup("NotSupportedError: encrypt");
    return NULL;
}

/* SubtleCrypto decrypt dispatch for AES and RSA-OAEP; caller frees. */
guint8 *
ns_crypto_decrypt(const ns_crypto_key *k, const ns_crypto_params *p,
                  const guint8 *data, gsize len, gsize *out_len, char **err)
{
    if (ns_crypto_is_aes(k->algo))
        return ns_crypto_aes(k, p, data, len, FALSE, out_len, err);
    if (!g_strcmp0(k->algo, "RSA-OAEP"))
        return ns_crypto_rsa_oaep(k, p, data, len, FALSE, out_len, err);
    if (err) *err = g_strdup("NotSupportedError: decrypt");
    return NULL;
}

/* Constant-time check that a buffer is all zero bytes. */
static gboolean
ns_crypto_all_zero(const guint8 *data, gsize len)
{
    guint8 any = 0;
    for (gsize i = 0; i < len; i++) any |= data[i];
    return any == 0;
}

/* ECDH/X25519/X448 shared secret, truncated to length_bits; rejects all-zero XDH output. */
static guint8 *
ns_crypto_ecdh(const ns_crypto_key *k, const ns_crypto_params *p, int length_bits,
               gsize *out_len, char **err)
{
    if (!k->pkey || !p->peer || !p->peer->pkey) {
        if (err) *err = g_strdup("InvalidAccessError: ECDH");
        return NULL;
    }
    size_t n = 0;
    guint8 *out = ns_pkey_derive(ns_pkey_of(k), ns_pkey_of(p->peer), &n);
    if (!out) return ns_crypto_err(err, "OperationError: ECDH");
    if (ns_crypto_is_xdh(k->algo) && ns_crypto_all_zero(out, n)) {
        g_free(out);
        if (err) *err = g_strdup("OperationError: all-zero shared secret");
        return NULL;
    }
    if (length_bits > 0) {
        gsize want = (gsize)length_bits / 8;
        if (want > n) {
            ns_crypto_cleanse(out, n);
            g_free(out);
            return ns_crypto_err(err, "OperationError: ECDH length");
        }
        n = want;
    }
    *out_len = n;
    return out;
}

/* Derives length_bits with PBKDF2-HMAC using the given salt and iterations. */
static guint8 *
ns_crypto_pbkdf2(const ns_crypto_key *k, const ns_crypto_params *p, int length_bits,
                 gsize *out_len, char **err)
{
    NsHash h;
    if (!ns_crypto_hash_of(p->kdf_hash, TRUE, &h) || length_bits <= 0) {
        if (err) *err = g_strdup("OperationError: PBKDF2");
        return NULL;
    }
    if (p->iterations <= 0) {
        if (err) *err = g_strdup("OperationError: PBKDF2 iterations must be positive");
        return NULL;
    }
    if (k->raw_len > (gsize)G_MAXINT || p->salt_len > (gsize)G_MAXINT) {
        if (err) *err = g_strdup("OperationError: PBKDF2 input too large");
        return NULL;
    }
    gsize n = (gsize)length_bits / 8;
    guint8 *out = g_malloc(n ? n : 1);
    if (!ns_pbkdf2(h, k->raw, k->raw_len, p->salt, p->salt_len,
                   (guint32)p->iterations, out, n)) {
        ns_crypto_cleanse(out, n ? n : 1);
        g_free(out);
        return ns_crypto_err(err, "OperationError: PBKDF2");
    }
    *out_len = n;
    return out;
}

/* Derives length_bits with HKDF using the given hash, salt and info. */
static guint8 *
ns_crypto_hkdf(const ns_crypto_key *k, const ns_crypto_params *p, int length_bits,
               gsize *out_len, char **err)
{
    NsHash h;
    if (!ns_crypto_hash_of(p->kdf_hash, FALSE, &h) || length_bits <= 0) {
        if (err) *err = g_strdup("OperationError: HKDF");
        return NULL;
    }
    gsize n = (gsize)length_bits / 8;
    guint8 *out = g_malloc(n ? n : 1);
    if (!ns_hkdf(h, k->raw ? k->raw : (const guint8 *)"", k->raw_len,
                 p->salt, p->salt_len, p->info, p->info_len, out, n)) {
        ns_crypto_cleanse(out, n ? n : 1);
        g_free(out);
        return ns_crypto_err(err, "OperationError: HKDF");
    }
    *out_len = n;
    return out;
}

/* SubtleCrypto deriveBits dispatch for ECDH, XDH, PBKDF2 and HKDF; caller frees. */
guint8 *
ns_crypto_derive_bits(const ns_crypto_key *k, const ns_crypto_params *p,
                      int length_bits, gsize *out_len, char **err)
{
    if (length_bits > (1 << 20)) {
        if (err) *err = g_strdup("OperationError: deriveBits length too large");
        return NULL;
    }
    if (!g_strcmp0(k->algo, "ECDH") || ns_crypto_is_xdh(k->algo))
        return ns_crypto_ecdh(k, p, length_bits, out_len, err);
    if (!g_strcmp0(k->algo, "PBKDF2"))
        return ns_crypto_pbkdf2(k, p, length_bits, out_len, err);
    if (!g_strcmp0(k->algo, "HKDF"))
        return ns_crypto_hkdf(k, p, length_bits, out_len, err);
    if (err) *err = g_strdup("NotSupportedError: deriveBits");
    return NULL;
}
