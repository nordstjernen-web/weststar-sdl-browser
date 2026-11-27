/* Weststar SDL Browser — the platform layer: cryptography.
 *
 * The primitives Weststar's own code needs (Web Crypto in webcrypto.c,
 * random numbers and the version string in net.c, SHA-256 for Safe
 * Browsing), over one of two libraries chosen with -Dcrypto:
 * ns_crypto_openssl.c (OpenSSL libcrypto, the default) or
 * ns_crypto_mbedtls.c (Mbed TLS 3.6, for portable builds). HTTPS is not
 * here: libcurl brings its own TLS library.
 *
 * - The API speaks bytes and small enums, not WebCrypto names or library
 *   objects; webcrypto.c maps the JS-facing algorithms onto it and keeps the
 *   WebCrypto rules (parameter checks, the AES-CTR counter wrap, small-order
 *   EdDSA points, truncating derived bits) out of the backends.
 * - Buffers returned as uint8_t * come from malloc(): free them with free()
 *   or g_free(). Functions returning NULL or false have failed; the library's
 *   reason, if it gave one, is then kept for ns_crypto_error_take().
 * - An algorithm the library cannot do reports false from its _available()
 *   function, and its operations fail (Mbed TLS 3.6 has no EdDSA).
 * - Everything is safe to call from any thread; an NsPkey is not modified
 *   by the operations that take it const.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_CRYPTO_H
#define NS_CRYPTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- The library ---- */

/* Name and version of the crypto library, e.g. "OpenSSL 3.5.0 8 Apr 2025". */
const char *ns_crypto_library_version(void);

/* Fills buf with len bytes from the library's CSPRNG. */
bool ns_crypto_random(void *buf, size_t len);

/* Copies the reason for the last failure on this thread into buf (if any)
 * and forgets it; false if the library gave none. */
bool ns_crypto_error_take(char *buf, size_t size);

/* Forgets any failure reason on this thread. */
void ns_crypto_error_clear(void);

/* Overwrites len bytes with zeros in a way the compiler keeps. */
void ns_crypto_cleanse(void *buf, size_t len);

/* Compares in time independent of the contents; true if equal. */
bool ns_crypto_memeq(const void *a, const void *b, size_t len);

/* ---- Hashes, HMAC and key derivation ---- */

typedef enum {
    NS_HASH_SHA1,
    NS_HASH_SHA256,
    NS_HASH_SHA384,
    NS_HASH_SHA512,
    NS_HASH_SHA3_256,
    NS_HASH_SHA3_384,
    NS_HASH_SHA3_512,
} NsHash;

#define NS_HASH_MAX_SIZE 64

/* Digest length in bytes. */
static inline size_t
ns_hash_size(NsHash h)
{
    switch (h) {
    case NS_HASH_SHA1:     return 20;
    case NS_HASH_SHA256:   return 32;
    case NS_HASH_SHA3_256: return 32;
    case NS_HASH_SHA384:   return 48;
    case NS_HASH_SHA3_384: return 48;
    default:               return 64;
    }
}

/* True if the library implements the hash. */
bool ns_hash_available(NsHash h);

/* One-shot digest into out (ns_hash_size(h) bytes). */
bool ns_hash(NsHash h, const void *data, size_t len, uint8_t *out);

/* Incremental digest: new, update any number of times, finish (which frees
 * the context whether or not it succeeds). */
typedef struct NsHashCtx NsHashCtx;
NsHashCtx *ns_hash_new(NsHash h);
bool ns_hash_update(NsHashCtx *ctx, const void *data, size_t len);
bool ns_hash_finish(NsHashCtx *ctx, uint8_t *out);

/* HMAC of data under key into out (ns_hash_size(h) bytes). */
bool ns_hmac(NsHash h, const uint8_t *key, size_t key_len,
             const uint8_t *data, size_t len, uint8_t *out);

/* PBKDF2-HMAC (RFC 8018) of out_len bytes. */
bool ns_pbkdf2(NsHash h, const uint8_t *password, size_t password_len,
               const uint8_t *salt, size_t salt_len, uint32_t iterations,
               uint8_t *out, size_t out_len);

/* HKDF extract-and-expand (RFC 5869) of out_len bytes. */
bool ns_hkdf(NsHash h, const uint8_t *ikm, size_t ikm_len,
             const uint8_t *salt, size_t salt_len,
             const uint8_t *info, size_t info_len,
             uint8_t *out, size_t out_len);

/* ---- AES (keys of 16, 24 or 32 bytes) ---- */

/* CBC with PKCS#7 padding. out has room for len + 16 bytes; *out_len gets
 * the bytes written. Decryption fails on bad padding. */
bool ns_aes_cbc(bool encrypt, const uint8_t *key, size_t key_len,
                const uint8_t iv[16], const uint8_t *in, size_t len,
                uint8_t *out, size_t *out_len);

/* CTR from the 16-byte counter block, counting through all 128 bits
 * (big-endian); encryption and decryption are the same. */
bool ns_aes_ctr(const uint8_t *key, size_t key_len, const uint8_t counter[16],
                const uint8_t *in, size_t len, uint8_t *out);

/* GCM. Encryption writes len bytes to out and tag_len (4..16) to tag;
 * decryption fails unless the tag matches. */
bool ns_aes_gcm_encrypt(const uint8_t *key, size_t key_len,
                        const uint8_t *iv, size_t iv_len,
                        const uint8_t *aad, size_t aad_len,
                        const uint8_t *in, size_t len, uint8_t *out,
                        uint8_t *tag, size_t tag_len);
bool ns_aes_gcm_decrypt(const uint8_t *key, size_t key_len,
                        const uint8_t *iv, size_t iv_len,
                        const uint8_t *aad, size_t aad_len,
                        const uint8_t *in, size_t len,
                        const uint8_t *tag, size_t tag_len, uint8_t *out);

/* Key wrap (RFC 3394) of a multiple of 8 bytes, at least 16: wrapping
 * writes len + 8 bytes, unwrapping len - 8 and fails on a bad check value. */
bool ns_aes_kw_wrap(const uint8_t *key, size_t key_len,
                    const uint8_t *in, size_t len, uint8_t *out);
bool ns_aes_kw_unwrap(const uint8_t *key, size_t key_len,
                      const uint8_t *in, size_t len, uint8_t *out);

/* ---- Public-key pairs ---- */

typedef enum {
    NS_PKEY_RSA,
    NS_PKEY_EC,
    NS_PKEY_ED25519,
    NS_PKEY_X25519,
    NS_PKEY_ED448,
    NS_PKEY_X448,
} NsPkeyType;

typedef enum {
    NS_CURVE_NONE,
    NS_CURVE_P256,
    NS_CURVE_P384,
    NS_CURVE_P521,
} NsCurve;

/* A public key, or a private key with its public half. */
typedef struct NsPkey NsPkey;

/* True if the library implements keys of the type. */
bool ns_pkey_type_available(NsPkeyType type);

void        ns_pkey_free(NsPkey *key);
NsPkey     *ns_pkey_dup(const NsPkey *key);
NsPkeyType  ns_pkey_type(const NsPkey *key);
/* The curve of an EC key; NS_CURVE_NONE for other types. */
NsCurve     ns_pkey_curve(const NsPkey *key);
/* Key size in bits, as OpenSSL counts it (the modulus for RSA, the group
 * order for EC; 253, 256, 448 and 456 for X25519, Ed25519, X448, Ed448). */
int         ns_pkey_bits(const NsPkey *key);

NsPkey *ns_pkey_generate_rsa(int modulus_bits, uint32_t public_exponent);
NsPkey *ns_pkey_generate_ec(NsCurve curve);
NsPkey *ns_pkey_generate_okp(NsPkeyType type);

/* DER SubjectPublicKeyInfo and PKCS#8 PrivateKeyInfo, which must fill the
 * whole buffer, for RSA, EC on the three curves and the OKP types. */
NsPkey  *ns_pkey_from_spki(const uint8_t *der, size_t len);
NsPkey  *ns_pkey_from_pkcs8(const uint8_t *der, size_t len);
uint8_t *ns_pkey_to_spki(const NsPkey *key, size_t *len);
uint8_t *ns_pkey_to_pkcs8(const NsPkey *key, size_t *len);

/* RSA from big-endian components. Without D the key is public; P and Q,
 * and with them DP, DQ and QI, may be left out (len 0). */
typedef enum {
    NS_RSA_N, NS_RSA_E, NS_RSA_D, NS_RSA_P, NS_RSA_Q,
    NS_RSA_DP, NS_RSA_DQ, NS_RSA_QI,
    NS_RSA_PARTS
} NsRsaPart;

typedef struct {
    const uint8_t *data;
    size_t len;
} NsBytes;

NsPkey  *ns_pkey_from_rsa(const NsBytes parts[NS_RSA_PARTS]);
/* One component as minimal big-endian bytes; NULL if the key lacks it. */
uint8_t *ns_pkey_rsa_part(const NsPkey *key, NsRsaPart part, size_t *len);

/* EC from a SEC1 point, and with a private scalar d (big-endian), which
 * must belong to the point. */
NsPkey  *ns_pkey_from_ec_point(NsCurve curve, const uint8_t *point, size_t len);
NsPkey  *ns_pkey_from_ec_private(NsCurve curve, const uint8_t *point,
                                 size_t point_len, const uint8_t *d,
                                 size_t d_len);
/* The public point, uncompressed (0x04 || X || Y). */
uint8_t *ns_pkey_ec_point(const NsPkey *key, size_t *len);
/* The private scalar as minimal big-endian bytes. */
uint8_t *ns_pkey_ec_private(const NsPkey *key, size_t *len);

/* Ed25519/X25519/Ed448/X448 from the raw RFC 8032/7748 encoding of the
 * public or (private true) the private key. */
NsPkey  *ns_pkey_from_okp(NsPkeyType type, const uint8_t *data, size_t len,
                          bool private_key);
uint8_t *ns_pkey_okp_public(const NsPkey *key, size_t *len);
uint8_t *ns_pkey_okp_private(const NsPkey *key, size_t *len);

/* ---- Public-key operations ---- */

typedef enum {
    NS_SIG_RSA_PKCS1,  /* RSASSA-PKCS1-v1_5 */
    NS_SIG_RSA_PSS,    /* MGF1 with the same hash */
    NS_SIG_ECDSA,      /* signature as fixed-width r || s */
    NS_SIG_EDDSA,      /* pure Ed25519/Ed448; hash ignored */
} NsSigScheme;

typedef struct {
    NsSigScheme scheme;
    NsHash hash;
    size_t pss_salt_len;
    const uint8_t *context; /* Ed448 only */
    size_t context_len;
} NsSigParams;

/* True if Ed448 signing and verification take a context string. */
bool ns_pkey_eddsa_context_supported(void);

uint8_t *ns_pkey_sign(const NsPkey *key, const NsSigParams *p,
                      const uint8_t *data, size_t len, size_t *sig_len);
/* 1 if the signature is valid, 0 if not, -1 if the key cannot verify. */
int      ns_pkey_verify(const NsPkey *key, const NsSigParams *p,
                        const uint8_t *sig, size_t sig_len,
                        const uint8_t *data, size_t len);

/* RSA-OAEP with the hash for both OAEP and MGF1, and an optional label. */
uint8_t *ns_pkey_rsa_oaep(const NsPkey *key, bool encrypt, NsHash hash,
                          const uint8_t *label, size_t label_len,
                          const uint8_t *in, size_t len, size_t *out_len);

/* ECDH or X25519/X448 shared secret between a private key and a peer's
 * public key of the same type and curve (for EC, the X coordinate). */
uint8_t *ns_pkey_derive(const NsPkey *key, const NsPkey *peer, size_t *len);

#endif /* NS_CRYPTO_H */
