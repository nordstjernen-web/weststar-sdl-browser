/* Weststar SDL Browser — the platform layer: cryptography over Mbed TLS 3.6
 * (-Dcrypto=mbedtls, for portable builds). See ns_crypto.h.
 *
 * Only Mbed TLS's crypto library is used, through its classic (non-PSA)
 * API, so the stock mbedtls_config.h serves; a system copy needs SHA-1/2,
 * AES with GCM, RSA with PKCS#1 v1.5 and v2.1, ECDSA, ECDH, HKDF, PKCS#5,
 * CTR_DRBG and an entropy source, and the P-256/384/521, Curve25519 and
 * Curve448 curves. SHA-3 is used when the library has it.
 *
 * - Random numbers come from one CTR_DRBG, seeded from the platform entropy
 *   source on first use and shared under a lock.
 * - Mbed TLS 3.6 has no EdDSA, so Ed25519 and Ed448 are unavailable.
 * - SPKI and PKCS#8 are read and written here with Mbed TLS's ASN.1 reader
 *   and a small DER writer, in the same encodings OpenSSL produces (an EC
 *   PKCS#8 key holds its public point and no curve parameters; X25519 and
 *   X448 private keys keep the bytes they were given, clamped only for use).
 * - RSA and EC contexts change during private-key operations (blinding
 *   values, cached tables) and RSA's padding is a context setting, so each
 *   key carries a lock its operations take.
 * - RSA key generation needs an even modulus length of at least
 *   MBEDTLS_RSA_GEN_KEY_MIN_BITS (1024) and at most MBEDTLS_MPI_MAX_BITS.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "plat/ns_crypto.h"
#include "plat/ns_plat.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <mbedtls/aes.h>
#include <mbedtls/asn1.h>
#include <mbedtls/bignum.h>
#include <mbedtls/build_info.h>
#include <mbedtls/constant_time.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/gcm.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/rsa.h>

/* ---- Errors ---- */

static NsPrivate g_last_error = NS_PRIVATE_INIT(NULL);

/* Remembers a failing Mbed TLS return code for ns_crypto_error_take(). */
static void
set_error(int ret)
{
    if (ret) ns_private_set(&g_last_error, (void *)(intptr_t)ret);
}

/* Returns true for ret == 0, remembering any failure. */
static bool
ok_ret(int ret)
{
    set_error(ret);
    return ret == 0;
}

bool
ns_crypto_error_take(char *buf, size_t size)
{
    int ret = (int)(intptr_t)ns_private_get(&g_last_error);
    ns_private_set(&g_last_error, NULL);
    if (!ret) return false;
    if (buf && size) {
#if defined(MBEDTLS_ERROR_C)
        mbedtls_strerror(ret, buf, size);
#else
        SDL_snprintf(buf, size, "Mbed TLS error -0x%04X", (unsigned)-ret);
#endif
    }
    return true;
}

void
ns_crypto_error_clear(void)
{
    ns_private_set(&g_last_error, NULL);
}

/* ---- The library and random numbers ---- */

const char *
ns_crypto_library_version(void)
{
    return MBEDTLS_VERSION_STRING_FULL;
}

void
ns_crypto_cleanse(void *buf, size_t len)
{
    if (buf && len) mbedtls_platform_zeroize(buf, len);
}

bool
ns_crypto_memeq(const void *a, const void *b, size_t len)
{
    return mbedtls_ct_memcmp(a, b, len) == 0;
}

static NsOnce g_rng_once;
static NsMutex g_rng_lock;
static mbedtls_entropy_context g_entropy;
static mbedtls_ctr_drbg_context g_drbg;
static bool g_rng_ready;

/* The f_rng callback Mbed TLS functions take: the shared CTR_DRBG. */
static int
rng(void *ctx, unsigned char *out, size_t len)
{
    (void)ctx;
    if (ns_once_enter(&g_rng_once)) {
        static const char pers[] = "Weststar ns_crypto";
        mbedtls_entropy_init(&g_entropy);
        mbedtls_ctr_drbg_init(&g_drbg);
        g_rng_ready = ok_ret(mbedtls_ctr_drbg_seed(&g_drbg, mbedtls_entropy_func,
                                                   &g_entropy,
                                                   (const unsigned char *)pers,
                                                   sizeof pers - 1));
        ns_once_leave(&g_rng_once);
    }
    if (!g_rng_ready) return MBEDTLS_ERR_CTR_DRBG_ENTROPY_SOURCE_FAILED;
    int ret = 0;
    ns_mutex_lock(&g_rng_lock);
    while (!ret && len) {
        size_t n = len > MBEDTLS_CTR_DRBG_MAX_REQUEST ? MBEDTLS_CTR_DRBG_MAX_REQUEST
                                                      : len;
        ret = mbedtls_ctr_drbg_random(&g_drbg, out, n);
        out += n;
        len -= n;
    }
    ns_mutex_unlock(&g_rng_lock);
    return ret;
}

bool
ns_crypto_random(void *buf, size_t len)
{
    return ok_ret(rng(NULL, buf, len));
}

/* ---- Hashes, HMAC and key derivation ---- */

static mbedtls_md_type_t
md_type(NsHash h)
{
    switch (h) {
    case NS_HASH_SHA1:     return MBEDTLS_MD_SHA1;
    case NS_HASH_SHA256:   return MBEDTLS_MD_SHA256;
    case NS_HASH_SHA384:   return MBEDTLS_MD_SHA384;
    case NS_HASH_SHA512:   return MBEDTLS_MD_SHA512;
    case NS_HASH_SHA3_256: return MBEDTLS_MD_SHA3_256;
    case NS_HASH_SHA3_384: return MBEDTLS_MD_SHA3_384;
    case NS_HASH_SHA3_512: return MBEDTLS_MD_SHA3_512;
    }
    return MBEDTLS_MD_NONE;
}

static const mbedtls_md_info_t *
md_info(NsHash h)
{
    return mbedtls_md_info_from_type(md_type(h));
}

bool
ns_hash_available(NsHash h)
{
    return md_info(h) != NULL;
}

bool
ns_hash(NsHash h, const void *data, size_t len, uint8_t *out)
{
    const mbedtls_md_info_t *md = md_info(h);
    return md && ok_ret(mbedtls_md(md, data ? data : (const void *)"", len, out));
}

struct NsHashCtx {
    mbedtls_md_context_t md;
};

NsHashCtx *
ns_hash_new(NsHash h)
{
    const mbedtls_md_info_t *md = md_info(h);
    NsHashCtx *ctx = md ? malloc(sizeof *ctx) : NULL;
    if (!ctx) return NULL;
    mbedtls_md_init(&ctx->md);
    if (!ok_ret(mbedtls_md_setup(&ctx->md, md, 0)) ||
        !ok_ret(mbedtls_md_starts(&ctx->md))) {
        mbedtls_md_free(&ctx->md);
        free(ctx);
        return NULL;
    }
    return ctx;
}

bool
ns_hash_update(NsHashCtx *ctx, const void *data, size_t len)
{
    return !len || ok_ret(mbedtls_md_update(&ctx->md, data, len));
}

bool
ns_hash_finish(NsHashCtx *ctx, uint8_t *out)
{
    bool ok = ok_ret(mbedtls_md_finish(&ctx->md, out));
    mbedtls_md_free(&ctx->md);
    free(ctx);
    return ok;
}

bool
ns_hmac(NsHash h, const uint8_t *key, size_t key_len, const uint8_t *data,
        size_t len, uint8_t *out)
{
    const mbedtls_md_info_t *md = md_info(h);
    return md && ok_ret(mbedtls_md_hmac(md, key ? key : (const uint8_t *)"",
                                        key_len,
                                        data ? data : (const uint8_t *)"",
                                        len, out));
}

bool
ns_pbkdf2(NsHash h, const uint8_t *password, size_t password_len,
          const uint8_t *salt, size_t salt_len, uint32_t iterations,
          uint8_t *out, size_t out_len)
{
    if (!md_info(h) || out_len > UINT32_MAX || iterations > UINT_MAX) return false;
    return ok_ret(mbedtls_pkcs5_pbkdf2_hmac_ext(
        md_type(h), password ? password : (const uint8_t *)"", password_len,
        salt ? salt : (const uint8_t *)"", salt_len, (unsigned int)iterations,
        (uint32_t)out_len, out));
}

bool
ns_hkdf(NsHash h, const uint8_t *ikm, size_t ikm_len, const uint8_t *salt,
        size_t salt_len, const uint8_t *info, size_t info_len, uint8_t *out,
        size_t out_len)
{
    const mbedtls_md_info_t *md = md_info(h);
    return md && ok_ret(mbedtls_hkdf(md, salt ? salt : (const uint8_t *)"",
                                     salt_len, ikm ? ikm : (const uint8_t *)"",
                                     ikm_len, info ? info : (const uint8_t *)"",
                                     info_len, out, out_len));
}

/* ---- AES ---- */

static bool
aes_key_ok(size_t key_len)
{
    return key_len == 16 || key_len == 24 || key_len == 32;
}

bool
ns_aes_cbc(bool encrypt, const uint8_t *key, size_t key_len,
           const uint8_t iv[16], const uint8_t *in, size_t len, uint8_t *out,
           size_t *out_len)
{
    if (!aes_key_ok(key_len) || (!encrypt && (len == 0 || len % 16)))
        return false;
    mbedtls_aes_context aes;
    unsigned char ivbuf[16];
    memcpy(ivbuf, iv, 16);
    mbedtls_aes_init(&aes);
    bool ok;
    if (encrypt) {
        /* PKCS#7: pad to a whole block, always adding at least one byte. */
        size_t pad = 16 - len % 16;
        if (len) memmove(out, in, len);
        memset(out + len, (int)pad, pad);
        ok = ok_ret(mbedtls_aes_setkey_enc(&aes, key, (unsigned)key_len * 8)) &&
             ok_ret(mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, len + pad,
                                          ivbuf, out, out));
        if (ok) *out_len = len + pad;
    } else {
        ok = ok_ret(mbedtls_aes_setkey_dec(&aes, key, (unsigned)key_len * 8)) &&
             ok_ret(mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, len, ivbuf,
                                          in, out));
        if (ok) {
            /* Checks the padding without branching on its bytes. */
            unsigned pad = out[len - 1];
            unsigned bad = (unsigned)(pad == 0) | (unsigned)(pad > 16);
            for (size_t i = 1; i <= 16; i++) {
                unsigned in_pad = (unsigned)(i <= pad);
                bad |= in_pad & (unsigned)(out[len - i] != pad);
            }
            ok = !bad;
            if (ok) *out_len = len - pad;
        }
    }
    mbedtls_aes_free(&aes);
    return ok;
}

bool
ns_aes_ctr(const uint8_t *key, size_t key_len, const uint8_t counter[16],
           const uint8_t *in, size_t len, uint8_t *out)
{
    if (!aes_key_ok(key_len)) return false;
    mbedtls_aes_context aes;
    unsigned char nonce[16], stream[16];
    size_t off = 0;
    memcpy(nonce, counter, 16);
    mbedtls_aes_init(&aes);
    bool ok = ok_ret(mbedtls_aes_setkey_enc(&aes, key, (unsigned)key_len * 8)) &&
              (!len || ok_ret(mbedtls_aes_crypt_ctr(&aes, len, &off, nonce, stream,
                                                    in, out)));
    mbedtls_aes_free(&aes);
    mbedtls_platform_zeroize(stream, sizeof stream);
    return ok;
}

bool
ns_aes_gcm_encrypt(const uint8_t *key, size_t key_len, const uint8_t *iv,
                   size_t iv_len, const uint8_t *aad, size_t aad_len,
                   const uint8_t *in, size_t len, uint8_t *out, uint8_t *tag,
                   size_t tag_len)
{
    if (!aes_key_ok(key_len)) return false;
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    bool ok = ok_ret(mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key,
                                        (unsigned)key_len * 8)) &&
              ok_ret(mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, len, iv,
                                               iv_len, aad, aad_len, in, out,
                                               tag_len, tag));
    mbedtls_gcm_free(&gcm);
    return ok;
}

bool
ns_aes_gcm_decrypt(const uint8_t *key, size_t key_len, const uint8_t *iv,
                   size_t iv_len, const uint8_t *aad, size_t aad_len,
                   const uint8_t *in, size_t len, const uint8_t *tag,
                   size_t tag_len, uint8_t *out)
{
    if (!aes_key_ok(key_len)) return false;
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    bool ok = ok_ret(mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key,
                                        (unsigned)key_len * 8)) &&
              ok_ret(mbedtls_gcm_auth_decrypt(&gcm, len, iv, iv_len, aad, aad_len,
                                              tag, tag_len, in, out));
    mbedtls_gcm_free(&gcm);
    return ok;
}

/* RFC 3394 over single AES blocks, so it needs no NIST_KW module. */
static const uint8_t kw_iv[8] = { 0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6, 0xa6 };

bool
ns_aes_kw_wrap(const uint8_t *key, size_t key_len, const uint8_t *in,
               size_t len, uint8_t *out)
{
    if (!aes_key_ok(key_len) || len < 16 || len % 8) return false;
    size_t n = len / 8;
    mbedtls_aes_context aes;
    uint8_t a[8], b[16];
    mbedtls_aes_init(&aes);
    bool ok = ok_ret(mbedtls_aes_setkey_enc(&aes, key, (unsigned)key_len * 8));
    memcpy(a, kw_iv, 8);
    memmove(out + 8, in, len);
    for (uint64_t j = 0; ok && j < 6; j++) {
        for (size_t i = 1; ok && i <= n; i++) {
            memcpy(b, a, 8);
            memcpy(b + 8, out + 8 * i, 8);
            ok = ok_ret(mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, b, b));
            uint64_t t = n * j + i;
            for (int k = 0; k < 8; k++) a[k] = b[k] ^ (uint8_t)(t >> (56 - 8 * k));
            memcpy(out + 8 * i, b + 8, 8);
        }
    }
    memcpy(out, a, 8);
    mbedtls_aes_free(&aes);
    mbedtls_platform_zeroize(b, sizeof b);
    return ok;
}

bool
ns_aes_kw_unwrap(const uint8_t *key, size_t key_len, const uint8_t *in,
                 size_t len, uint8_t *out)
{
    if (!aes_key_ok(key_len) || len < 24 || len % 8) return false;
    size_t n = len / 8 - 1;
    mbedtls_aes_context aes;
    uint8_t a[8], b[16];
    mbedtls_aes_init(&aes);
    bool ok = ok_ret(mbedtls_aes_setkey_dec(&aes, key, (unsigned)key_len * 8));
    memcpy(a, in, 8);
    memmove(out, in + 8, len - 8);
    for (uint64_t j = 6; ok && j-- > 0;) {
        for (size_t i = n; ok && i >= 1; i--) {
            uint64_t t = n * j + i;
            for (int k = 0; k < 8; k++) b[k] = a[k] ^ (uint8_t)(t >> (56 - 8 * k));
            memcpy(b + 8, out + 8 * (i - 1), 8);
            ok = ok_ret(mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, b, b));
            memcpy(a, b, 8);
            memcpy(out + 8 * (i - 1), b + 8, 8);
        }
    }
    mbedtls_aes_free(&aes);
    mbedtls_platform_zeroize(b, sizeof b);
    if (ok && mbedtls_ct_memcmp(a, kw_iv, 8) != 0) ok = false;
    if (!ok) mbedtls_platform_zeroize(out, len - 8);
    return ok;
}

/* ---- Key pairs ---- */

#define OKP_MAX 57

struct NsPkey {
    NsPkeyType type;
    bool has_private;
    NsMutex lock;
    mbedtls_rsa_context rsa;       /* NS_PKEY_RSA */
    mbedtls_ecp_group grp;         /* NS_PKEY_EC, and X25519/X448 for math */
    mbedtls_mpi d;                 /* NS_PKEY_EC private scalar */
    mbedtls_ecp_point Q;           /* NS_PKEY_EC public point */
    uint8_t okp_pub[OKP_MAX];      /* X25519/X448: raw public key */
    uint8_t okp_priv[OKP_MAX];     /* and the raw private key as given */
    size_t okp_len;
};

static NsPkey *
pkey_new(NsPkeyType type)
{
    NsPkey *k = calloc(1, sizeof *k);
    if (!k) return NULL;
    k->type = type;
    mbedtls_rsa_init(&k->rsa);
    mbedtls_ecp_group_init(&k->grp);
    mbedtls_mpi_init(&k->d);
    mbedtls_ecp_point_init(&k->Q);
    return k;
}

void
ns_pkey_free(NsPkey *key)
{
    if (!key) return;
    mbedtls_rsa_free(&key->rsa);
    mbedtls_ecp_group_free(&key->grp);
    mbedtls_mpi_free(&key->d);
    mbedtls_ecp_point_free(&key->Q);
    ns_mutex_clear(&key->lock);
    mbedtls_platform_zeroize(key, sizeof *key);
    free(key);
}

/* Takes the key's lock for an operation on it. */
static NsPkey *
pkey_lock(const NsPkey *key)
{
    NsPkey *k = (NsPkey *)key;
    ns_mutex_lock(&k->lock);
    return k;
}

static void
pkey_unlock(NsPkey *k)
{
    ns_mutex_unlock(&k->lock);
}

static mbedtls_ecp_group_id
curve_id(NsCurve curve)
{
    switch (curve) {
    case NS_CURVE_P256: return MBEDTLS_ECP_DP_SECP256R1;
    case NS_CURVE_P384: return MBEDTLS_ECP_DP_SECP384R1;
    case NS_CURVE_P521: return MBEDTLS_ECP_DP_SECP521R1;
    default:            return MBEDTLS_ECP_DP_NONE;
    }
}

/* Raw key length of an X25519/X448 key, 0 for other types. */
static size_t
xdh_len(NsPkeyType type)
{
    return type == NS_PKEY_X25519 ? 32 : type == NS_PKEY_X448 ? 56 : 0;
}

bool
ns_pkey_type_available(NsPkeyType type)
{
    switch (type) {
    case NS_PKEY_RSA:
    case NS_PKEY_EC:
        return true;
    case NS_PKEY_X25519:
        return mbedtls_ecp_curve_info_from_grp_id(MBEDTLS_ECP_DP_CURVE25519) != NULL;
    case NS_PKEY_X448:
        return mbedtls_ecp_curve_info_from_grp_id(MBEDTLS_ECP_DP_CURVE448) != NULL;
    case NS_PKEY_ED25519:
    case NS_PKEY_ED448:
        return false;
    }
    return false;
}

NsPkey *
ns_pkey_dup(const NsPkey *key)
{
    NsPkey *k = pkey_new(key->type);
    if (!k) return NULL;
    NsPkey *src = pkey_lock(key);
    bool ok = ok_ret(mbedtls_rsa_copy(&k->rsa, &src->rsa)) &&
              (src->type == NS_PKEY_RSA ||
               ok_ret(mbedtls_ecp_group_copy(&k->grp, &src->grp))) &&
              ok_ret(mbedtls_mpi_copy(&k->d, &src->d)) &&
              ok_ret(mbedtls_ecp_copy(&k->Q, &src->Q));
    k->has_private = src->has_private;
    memcpy(k->okp_pub, src->okp_pub, OKP_MAX);
    memcpy(k->okp_priv, src->okp_priv, OKP_MAX);
    k->okp_len = src->okp_len;
    pkey_unlock(src);
    if (!ok) {
        ns_pkey_free(k);
        return NULL;
    }
    return k;
}

NsPkeyType
ns_pkey_type(const NsPkey *key)
{
    return key->type;
}

NsCurve
ns_pkey_curve(const NsPkey *key)
{
    if (!key || key->type != NS_PKEY_EC) return NS_CURVE_NONE;
    switch (key->grp.id) {
    case MBEDTLS_ECP_DP_SECP256R1: return NS_CURVE_P256;
    case MBEDTLS_ECP_DP_SECP384R1: return NS_CURVE_P384;
    case MBEDTLS_ECP_DP_SECP521R1: return NS_CURVE_P521;
    default:                       return NS_CURVE_NONE;
    }
}

int
ns_pkey_bits(const NsPkey *key)
{
    switch (key->type) {
    case NS_PKEY_RSA:     return (int)mbedtls_rsa_get_bitlen(&key->rsa);
    case NS_PKEY_EC:      return (int)key->grp.nbits;
    case NS_PKEY_X25519:  return 253;
    case NS_PKEY_ED25519: return 256;
    case NS_PKEY_X448:    return 448;
    case NS_PKEY_ED448:   return 456;
    }
    return 0;
}

/* The Montgomery group behind an X25519/X448 key. */
static mbedtls_ecp_group_id
xdh_group(NsPkeyType type)
{
    return type == NS_PKEY_X25519 ? MBEDTLS_ECP_DP_CURVE25519
                                  : MBEDTLS_ECP_DP_CURVE448;
}

/* An X25519/X448 private key as the clamped scalar RFC 7748 uses. */
static int
xdh_scalar(const NsPkey *k, mbedtls_mpi *d)
{
    uint8_t s[OKP_MAX];
    memcpy(s, k->okp_priv, k->okp_len);
    if (k->type == NS_PKEY_X25519) {
        s[0] &= 248;
        s[31] &= 127;
        s[31] |= 64;
    } else {
        s[0] &= 252;
        s[55] |= 128;
    }
    int ret = mbedtls_mpi_read_binary_le(d, s, k->okp_len);
    mbedtls_platform_zeroize(s, sizeof s);
    return ret;
}

/* Sets an X25519/X448 key from raw bytes, deriving the public half of a
 * private key. */
static NsPkey *
xdh_from_raw(NsPkeyType type, const uint8_t *data, size_t len, bool priv)
{
    size_t n = xdh_len(type);
    if (!n || len != n || !ns_pkey_type_available(type)) return NULL;
    NsPkey *k = pkey_new(type);
    if (!k) return NULL;
    k->okp_len = n;
    k->has_private = priv;
    bool ok = ok_ret(mbedtls_ecp_group_load(&k->grp, xdh_group(type)));
    if (ok && priv) {
        mbedtls_mpi d;
        mbedtls_ecp_point R;
        size_t out_len = 0;
        mbedtls_mpi_init(&d);
        mbedtls_ecp_point_init(&R);
        memcpy(k->okp_priv, data, n);
        ok = ok_ret(xdh_scalar(k, &d)) &&
             ok_ret(mbedtls_ecp_mul(&k->grp, &R, &d, &k->grp.G, rng, NULL)) &&
             ok_ret(mbedtls_ecp_point_write_binary(&k->grp, &R,
                                                   MBEDTLS_ECP_PF_COMPRESSED,
                                                   &out_len, k->okp_pub, n)) &&
             out_len == n;
        mbedtls_mpi_free(&d);
        mbedtls_ecp_point_free(&R);
    } else if (ok) {
        memcpy(k->okp_pub, data, n);
    }
    if (!ok) {
        ns_pkey_free(k);
        return NULL;
    }
    return k;
}

NsPkey *
ns_pkey_generate_rsa(int modulus_bits, uint32_t public_exponent)
{
    uint32_t e = public_exponent ? public_exponent : 65537;
    if (modulus_bits <= 0 || e > INT_MAX) return NULL;
    NsPkey *k = pkey_new(NS_PKEY_RSA);
    if (!k) return NULL;
    k->has_private = true;
    if (!ok_ret(mbedtls_rsa_gen_key(&k->rsa, rng, NULL, (unsigned)modulus_bits,
                                    (int)e))) {
        ns_pkey_free(k);
        return NULL;
    }
    return k;
}

NsPkey *
ns_pkey_generate_ec(NsCurve curve)
{
    mbedtls_ecp_group_id id = curve_id(curve);
    if (id == MBEDTLS_ECP_DP_NONE) return NULL;
    NsPkey *k = pkey_new(NS_PKEY_EC);
    if (!k) return NULL;
    k->has_private = true;
    if (!ok_ret(mbedtls_ecp_group_load(&k->grp, id)) ||
        !ok_ret(mbedtls_ecp_gen_keypair(&k->grp, &k->d, &k->Q, rng, NULL))) {
        ns_pkey_free(k);
        return NULL;
    }
    return k;
}

NsPkey *
ns_pkey_generate_okp(NsPkeyType type)
{
    size_t n = xdh_len(type);
    uint8_t raw[OKP_MAX];
    if (!n || !ns_pkey_type_available(type) || !ns_crypto_random(raw, n))
        return NULL;
    NsPkey *k = xdh_from_raw(type, raw, n, true);
    mbedtls_platform_zeroize(raw, sizeof raw);
    return k;
}

/* ---- RSA components ---- */

NsPkey *
ns_pkey_from_rsa(const NsBytes parts[NS_RSA_PARTS])
{
    bool priv = parts[NS_RSA_D].data && parts[NS_RSA_D].len;
    bool pq = priv && parts[NS_RSA_P].data && parts[NS_RSA_P].len &&
              parts[NS_RSA_Q].data && parts[NS_RSA_Q].len;
    if (!parts[NS_RSA_N].len || !parts[NS_RSA_E].len) return NULL;
    NsPkey *k = pkey_new(NS_PKEY_RSA);
    if (!k) return NULL;
    k->has_private = priv;
    bool ok = ok_ret(mbedtls_rsa_import_raw(&k->rsa,
        parts[NS_RSA_N].data, parts[NS_RSA_N].len,
        pq ? parts[NS_RSA_P].data : NULL, pq ? parts[NS_RSA_P].len : 0,
        pq ? parts[NS_RSA_Q].data : NULL, pq ? parts[NS_RSA_Q].len : 0,
        priv ? parts[NS_RSA_D].data : NULL, priv ? parts[NS_RSA_D].len : 0,
        parts[NS_RSA_E].data, parts[NS_RSA_E].len)) &&
        ok_ret(mbedtls_rsa_complete(&k->rsa)) &&
        ok_ret(priv ? mbedtls_rsa_check_privkey(&k->rsa)
                    : mbedtls_rsa_check_pubkey(&k->rsa));
    if (!ok) {
        ns_pkey_free(k);
        return NULL;
    }
    return k;
}

/* An MPI as minimal big-endian bytes (one byte allocated for zero). */
static uint8_t *
mpi_out(const mbedtls_mpi *x, size_t *len)
{
    size_t n = mbedtls_mpi_size(x);
    uint8_t *out = malloc(n ? n : 1);
    if (out && n && !ok_ret(mbedtls_mpi_write_binary(x, out, n))) {
        free(out);
        return NULL;
    }
    if (out) *len = n;
    return out;
}

/* Copies one RSA component into x; false if the key lacks it. */
static bool
rsa_get(const mbedtls_rsa_context *rsa, NsRsaPart part, mbedtls_mpi *x)
{
    int ret;
    switch (part) {
    case NS_RSA_N:  ret = mbedtls_rsa_export(rsa, x, NULL, NULL, NULL, NULL); break;
    case NS_RSA_P:  ret = mbedtls_rsa_export(rsa, NULL, x, NULL, NULL, NULL); break;
    case NS_RSA_Q:  ret = mbedtls_rsa_export(rsa, NULL, NULL, x, NULL, NULL); break;
    case NS_RSA_D:  ret = mbedtls_rsa_export(rsa, NULL, NULL, NULL, x, NULL); break;
    case NS_RSA_E:  ret = mbedtls_rsa_export(rsa, NULL, NULL, NULL, NULL, x); break;
    case NS_RSA_DP: ret = mbedtls_rsa_export_crt(rsa, x, NULL, NULL); break;
    case NS_RSA_DQ: ret = mbedtls_rsa_export_crt(rsa, NULL, x, NULL); break;
    case NS_RSA_QI: ret = mbedtls_rsa_export_crt(rsa, NULL, NULL, x); break;
    default:        return false;
    }
    return ret == 0;
}

uint8_t *
ns_pkey_rsa_part(const NsPkey *key, NsRsaPart part, size_t *len)
{
    if (!key || key->type != NS_PKEY_RSA) return NULL;
    if (part != NS_RSA_N && part != NS_RSA_E && !key->has_private) return NULL;
    mbedtls_mpi x;
    mbedtls_mpi_init(&x);
    NsPkey *k = pkey_lock(key);
    uint8_t *out = rsa_get(&k->rsa, part, &x) ? mpi_out(&x, len) : NULL;
    pkey_unlock(k);
    mbedtls_mpi_free(&x);
    return out;
}

/* ---- EC points and scalars ---- */

/* Sets the private scalar and public point of an EC key on the curve,
 * checking both. With has_q false the point is computed from d. */
static bool
ec_set(NsPkey *k, mbedtls_ecp_group_id id, const uint8_t *point,
       size_t point_len, const mbedtls_mpi *d)
{
    if (!ok_ret(mbedtls_ecp_group_load(&k->grp, id))) return false;
    if (d) {
        if (!ok_ret(mbedtls_mpi_copy(&k->d, d)) ||
            !ok_ret(mbedtls_ecp_check_privkey(&k->grp, &k->d)))
            return false;
        k->has_private = true;
    }
    if (point) {
        if (!ok_ret(mbedtls_ecp_point_read_binary(&k->grp, &k->Q, point, point_len)) ||
            !ok_ret(mbedtls_ecp_check_pubkey(&k->grp, &k->Q)))
            return false;
    }
    if (d) {
        mbedtls_ecp_point R;
        mbedtls_ecp_point_init(&R);
        bool ok = ok_ret(mbedtls_ecp_mul(&k->grp, &R, &k->d, &k->grp.G, rng, NULL));
        if (ok && point) ok = mbedtls_ecp_point_cmp(&R, &k->Q) == 0;
        else if (ok) ok = ok_ret(mbedtls_ecp_copy(&k->Q, &R));
        mbedtls_ecp_point_free(&R);
        return ok;
    }
    return point != NULL;
}

NsPkey *
ns_pkey_from_ec_point(NsCurve curve, const uint8_t *point, size_t len)
{
    return ns_pkey_from_ec_private(curve, point, len, NULL, 0);
}

NsPkey *
ns_pkey_from_ec_private(NsCurve curve, const uint8_t *point, size_t point_len,
                        const uint8_t *d, size_t d_len)
{
    mbedtls_ecp_group_id id = curve_id(curve);
    if (id == MBEDTLS_ECP_DP_NONE || !point) return NULL;
    NsPkey *k = pkey_new(NS_PKEY_EC);
    if (!k) return NULL;
    mbedtls_mpi dm;
    mbedtls_mpi_init(&dm);
    bool priv = d && d_len;
    bool ok = (!priv || ok_ret(mbedtls_mpi_read_binary(&dm, d, d_len))) &&
              ec_set(k, id, point, point_len, priv ? &dm : NULL);
    mbedtls_mpi_free(&dm);
    if (!ok) {
        ns_pkey_free(k);
        return NULL;
    }
    return k;
}

/* Writes an EC key's uncompressed point into buf (MBEDTLS_ECP_MAX_PT_LEN). */
static bool
ec_point_write(const NsPkey *k, uint8_t *buf, size_t *len)
{
    return ok_ret(mbedtls_ecp_point_write_binary(&k->grp, &k->Q,
                                                 MBEDTLS_ECP_PF_UNCOMPRESSED,
                                                 len, buf, MBEDTLS_ECP_MAX_PT_LEN));
}

uint8_t *
ns_pkey_ec_point(const NsPkey *key, size_t *len)
{
    uint8_t buf[MBEDTLS_ECP_MAX_PT_LEN];
    size_t n = 0;
    if (!key || key->type != NS_PKEY_EC || !ec_point_write(key, buf, &n))
        return NULL;
    uint8_t *out = malloc(n);
    if (out) {
        memcpy(out, buf, n);
        *len = n;
    }
    return out;
}

uint8_t *
ns_pkey_ec_private(const NsPkey *key, size_t *len)
{
    if (!key || key->type != NS_PKEY_EC || !key->has_private) return NULL;
    return mpi_out(&key->d, len);
}

/* ---- X25519 / X448 raw keys ---- */

NsPkey *
ns_pkey_from_okp(NsPkeyType type, const uint8_t *data, size_t len,
                 bool private_key)
{
    return xdh_from_raw(type, data, len, private_key);
}

static uint8_t *
copy_out(const void *data, size_t len)
{
    uint8_t *out = malloc(len ? len : 1);
    if (out && len) memcpy(out, data, len);
    return out;
}

uint8_t *
ns_pkey_okp_public(const NsPkey *key, size_t *len)
{
    if (!key || !xdh_len(key->type)) return NULL;
    uint8_t *out = copy_out(key->okp_pub, key->okp_len);
    if (out) *len = key->okp_len;
    return out;
}

uint8_t *
ns_pkey_okp_private(const NsPkey *key, size_t *len)
{
    if (!key || !xdh_len(key->type) || !key->has_private) return NULL;
    uint8_t *out = copy_out(key->okp_priv, key->okp_len);
    if (out) *len = key->okp_len;
    return out;
}

/* ---- DER: object identifiers ---- */

typedef struct {
    const uint8_t *p;
    size_t len;
} oid_t;

#define OID(...) { (const uint8_t[]){ __VA_ARGS__ }, sizeof((const uint8_t[]){ __VA_ARGS__ }) }

static const oid_t oid_rsa = OID(0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01);
static const oid_t oid_ec = OID(0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01);
static const oid_t oid_p256 = OID(0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07);
static const oid_t oid_p384 = OID(0x2b, 0x81, 0x04, 0x00, 0x22);
static const oid_t oid_p521 = OID(0x2b, 0x81, 0x04, 0x00, 0x23);
static const oid_t oid_x25519 = OID(0x2b, 0x65, 0x6e);
static const oid_t oid_x448 = OID(0x2b, 0x65, 0x6f);

static bool
oid_is(const mbedtls_asn1_buf *b, const oid_t *oid)
{
    return b->tag == MBEDTLS_ASN1_OID && b->len == oid->len &&
           memcmp(b->p, oid->p, oid->len) == 0;
}

static const oid_t *
curve_oid(mbedtls_ecp_group_id id)
{
    switch (id) {
    case MBEDTLS_ECP_DP_SECP256R1: return &oid_p256;
    case MBEDTLS_ECP_DP_SECP384R1: return &oid_p384;
    case MBEDTLS_ECP_DP_SECP521R1: return &oid_p521;
    default:                       return NULL;
    }
}

static mbedtls_ecp_group_id
curve_of_oid(const mbedtls_asn1_buf *b)
{
    if (oid_is(b, &oid_p256)) return MBEDTLS_ECP_DP_SECP256R1;
    if (oid_is(b, &oid_p384)) return MBEDTLS_ECP_DP_SECP384R1;
    if (oid_is(b, &oid_p521)) return MBEDTLS_ECP_DP_SECP521R1;
    return MBEDTLS_ECP_DP_NONE;
}

/* ---- DER: reading ---- */

#define SEQ (MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE)

/* Reads a tag whose contents run exactly to end. */
static bool
der_whole(unsigned char **p, const unsigned char *end, int tag)
{
    size_t len;
    return mbedtls_asn1_get_tag(p, end, &len, tag) == 0 && *p + len == end;
}

/* RSAPublicKey: SEQUENCE { n, e }. */
static NsPkey *
rsa_public_der(unsigned char *p, const unsigned char *end)
{
    NsPkey *k = pkey_new(NS_PKEY_RSA);
    if (!k) return NULL;
    mbedtls_mpi n, e;
    mbedtls_mpi_init(&n);
    mbedtls_mpi_init(&e);
    bool ok = der_whole(&p, end, SEQ) &&
              mbedtls_asn1_get_mpi(&p, end, &n) == 0 &&
              mbedtls_asn1_get_mpi(&p, end, &e) == 0 && p == end &&
              ok_ret(mbedtls_rsa_import(&k->rsa, &n, NULL, NULL, NULL, &e)) &&
              ok_ret(mbedtls_rsa_complete(&k->rsa)) &&
              ok_ret(mbedtls_rsa_check_pubkey(&k->rsa));
    mbedtls_mpi_free(&n);
    mbedtls_mpi_free(&e);
    if (!ok) {
        ns_pkey_free(k);
        return NULL;
    }
    return k;
}

/* RSAPrivateKey: SEQUENCE { 0, n, e, d, p, q, dp, dq, qi }. The CRT values
 * are checked by being recomputed. */
static NsPkey *
rsa_private_der(unsigned char *p, const unsigned char *end)
{
    NsPkey *k = pkey_new(NS_PKEY_RSA);
    if (!k) return NULL;
    k->has_private = true;
    mbedtls_mpi v[8];
    for (int i = 0; i < 8; i++) mbedtls_mpi_init(&v[i]);
    int version = -1;
    bool ok = der_whole(&p, end, SEQ) &&
              mbedtls_asn1_get_int(&p, end, &version) == 0 && version == 0;
    for (int i = 0; ok && i < 8; i++)
        ok = mbedtls_asn1_get_mpi(&p, end, &v[i]) == 0;
    ok = ok && p == end &&
         ok_ret(mbedtls_rsa_import(&k->rsa, &v[0], &v[3], &v[4], &v[2], &v[1])) &&
         ok_ret(mbedtls_rsa_complete(&k->rsa)) &&
         ok_ret(mbedtls_rsa_check_privkey(&k->rsa));
    if (ok) {
        mbedtls_mpi dp, dq, qi;
        mbedtls_mpi_init(&dp);
        mbedtls_mpi_init(&dq);
        mbedtls_mpi_init(&qi);
        ok = mbedtls_rsa_export_crt(&k->rsa, &dp, &dq, &qi) == 0 &&
             mbedtls_mpi_cmp_mpi(&dp, &v[5]) == 0 &&
             mbedtls_mpi_cmp_mpi(&dq, &v[6]) == 0 &&
             mbedtls_mpi_cmp_mpi(&qi, &v[7]) == 0;
        mbedtls_mpi_free(&dp);
        mbedtls_mpi_free(&dq);
        mbedtls_mpi_free(&qi);
    }
    for (int i = 0; i < 8; i++) mbedtls_mpi_free(&v[i]);
    if (!ok) {
        ns_pkey_free(k);
        return NULL;
    }
    return k;
}

/* ECPrivateKey: SEQUENCE { 1, OCTET STRING d, [0] curve OPTIONAL,
 * [1] BIT STRING point OPTIONAL }; the point is computed when absent. */
static NsPkey *
ec_private_der(mbedtls_ecp_group_id id, unsigned char *p, const unsigned char *end)
{
    NsPkey *k = pkey_new(NS_PKEY_EC);
    if (!k) return NULL;
    mbedtls_mpi d;
    mbedtls_mpi_init(&d);
    int version = -1;
    size_t len = 0;
    const uint8_t *point = NULL;
    size_t point_len = 0;
    bool ok = der_whole(&p, end, SEQ) &&
              mbedtls_asn1_get_int(&p, end, &version) == 0 && version == 1 &&
              mbedtls_asn1_get_tag(&p, end, &len, MBEDTLS_ASN1_OCTET_STRING) == 0 &&
              mbedtls_mpi_read_binary(&d, p, len) == 0;
    if (ok) p += len;
    if (ok && p < end &&
        mbedtls_asn1_get_tag(&p, end, &len, MBEDTLS_ASN1_CONTEXT_SPECIFIC |
                                            MBEDTLS_ASN1_CONSTRUCTED | 0) == 0) {
        mbedtls_asn1_buf oid = { 0 };
        const unsigned char *pend = p + len;
        ok = mbedtls_asn1_get_tag(&p, pend, &oid.len, MBEDTLS_ASN1_OID) == 0;
        oid.tag = MBEDTLS_ASN1_OID;
        oid.p = p;
        ok = ok && p + oid.len == pend && curve_of_oid(&oid) == id;
        p = (unsigned char *)pend;
    }
    if (ok && p < end) {
        ok = mbedtls_asn1_get_tag(&p, end, &len, MBEDTLS_ASN1_CONTEXT_SPECIFIC |
                                                 MBEDTLS_ASN1_CONSTRUCTED | 1) == 0 &&
             p + len == end &&
             mbedtls_asn1_get_bitstring_null(&p, end, &point_len) == 0 &&
             p + point_len == end;
        point = p;
        p += point_len;
    }
    ok = ok && p == end && ec_set(k, id, point, point_len, &d);
    mbedtls_mpi_free(&d);
    if (!ok) {
        ns_pkey_free(k);
        return NULL;
    }
    return k;
}

NsPkey *
ns_pkey_from_spki(const uint8_t *der, size_t len)
{
    unsigned char *p = (unsigned char *)der;
    const unsigned char *end = der + len;
    mbedtls_asn1_buf alg, params;
    size_t bits_len = 0;
    if (!len || !der_whole(&p, end, SEQ) ||
        mbedtls_asn1_get_alg(&p, end, &alg, &params) != 0 ||
        mbedtls_asn1_get_bitstring_null(&p, end, &bits_len) != 0 ||
        p + bits_len != end)
        return NULL;
    if (oid_is(&alg, &oid_rsa)) {
        if (params.tag != 0 && (params.tag != MBEDTLS_ASN1_NULL || params.len))
            return NULL;
        return rsa_public_der(p, end);
    }
    if (oid_is(&alg, &oid_ec)) {
        mbedtls_ecp_group_id id = curve_of_oid(&params);
        if (id == MBEDTLS_ECP_DP_NONE) return NULL;
        NsPkey *k = pkey_new(NS_PKEY_EC);
        if (k && !ec_set(k, id, p, bits_len, NULL)) {
            ns_pkey_free(k);
            k = NULL;
        }
        return k;
    }
    if (params.tag != 0) return NULL;
    if (oid_is(&alg, &oid_x25519)) return xdh_from_raw(NS_PKEY_X25519, p, bits_len, false);
    if (oid_is(&alg, &oid_x448)) return xdh_from_raw(NS_PKEY_X448, p, bits_len, false);
    return NULL;
}

NsPkey *
ns_pkey_from_pkcs8(const uint8_t *der, size_t len)
{
    unsigned char *p = (unsigned char *)der;
    const unsigned char *end = der + len;
    mbedtls_asn1_buf alg, params;
    int version = -1;
    size_t key_len = 0;
    /* PrivateKeyInfo / OneAsymmetricKey: SEQUENCE { version, algorithm,
     * OCTET STRING key, [0] attributes OPTIONAL, [1] public key OPTIONAL }. */
    if (!len || !der_whole(&p, end, SEQ) ||
        mbedtls_asn1_get_int(&p, end, &version) != 0 ||
        (version != 0 && version != 1) ||
        mbedtls_asn1_get_alg(&p, end, &alg, &params) != 0 ||
        mbedtls_asn1_get_tag(&p, end, &key_len, MBEDTLS_ASN1_OCTET_STRING) != 0)
        return NULL;
    unsigned char *key = p;
    const unsigned char *key_end = p + key_len;
    for (p += key_len; p < end;) {
        size_t skip = 0;
        if ((*p & MBEDTLS_ASN1_TAG_CLASS_MASK) != MBEDTLS_ASN1_CONTEXT_SPECIFIC ||
            mbedtls_asn1_get_tag(&p, end, &skip, *p) != 0)
            return NULL;
        p += skip;
    }
    if (oid_is(&alg, &oid_rsa)) {
        if (params.tag != 0 && (params.tag != MBEDTLS_ASN1_NULL || params.len))
            return NULL;
        return rsa_private_der(key, key_end);
    }
    if (oid_is(&alg, &oid_ec)) {
        mbedtls_ecp_group_id id = curve_of_oid(&params);
        return id == MBEDTLS_ECP_DP_NONE ? NULL : ec_private_der(id, key, key_end);
    }
    if (params.tag != 0) return NULL;
    NsPkeyType type = oid_is(&alg, &oid_x25519) ? NS_PKEY_X25519
                    : oid_is(&alg, &oid_x448) ? NS_PKEY_X448 : NS_PKEY_ED25519;
    size_t raw_len = 0;
    if (type == NS_PKEY_ED25519 ||
        mbedtls_asn1_get_tag(&key, key_end, &raw_len, MBEDTLS_ASN1_OCTET_STRING) != 0 ||
        key + raw_len != key_end)
        return NULL;
    return xdh_from_raw(type, key, raw_len, true);
}

/* ---- DER: writing ---- */

typedef struct {
    uint8_t *p;
    size_t len, cap;
    bool bad;
} der_t;

static void
der_put(der_t *d, const void *s, size_t n)
{
    if (d->bad || !n) return;
    if (d->len + n > d->cap) {
        size_t cap = (d->len + n) * 2 + 64;
        uint8_t *q = malloc(cap);
        if (!q) {
            d->bad = true;
            return;
        }
        if (d->len) memcpy(q, d->p, d->len);
        if (d->p) mbedtls_platform_zeroize(d->p, d->cap);
        free(d->p);
        d->p = q;
        d->cap = cap;
    }
    memcpy(d->p + d->len, s, n);
    d->len += n;
}

static void
der_free(der_t *d)
{
    if (d->p) mbedtls_platform_zeroize(d->p, d->cap);
    free(d->p);
    d->p = NULL;
    d->len = d->cap = 0;
}

/* Appends a tag and length header. */
static void
der_head(der_t *d, uint8_t tag, size_t n)
{
    uint8_t h[6];
    size_t i = 0;
    h[i++] = tag;
    if (n < 0x80) {
        h[i++] = (uint8_t)n;
    } else if (n < 0x100) {
        h[i++] = 0x81;
        h[i++] = (uint8_t)n;
    } else if (n < 0x10000) {
        h[i++] = 0x82;
        h[i++] = (uint8_t)(n >> 8);
        h[i++] = (uint8_t)n;
    } else if (n < 0x1000000) {
        h[i++] = 0x83;
        h[i++] = (uint8_t)(n >> 16);
        h[i++] = (uint8_t)(n >> 8);
        h[i++] = (uint8_t)n;
    } else {
        d->bad = true;
        return;
    }
    der_put(d, h, i);
}

/* Appends a tagged value holding inner's bytes, and frees inner. */
static void
der_wrap(der_t *d, uint8_t tag, der_t *inner)
{
    if (inner->bad) d->bad = true;
    der_head(d, tag, inner->len);
    der_put(d, inner->p, inner->len);
    der_free(inner);
}

static void
der_raw(der_t *d, uint8_t tag, const void *s, size_t n)
{
    der_head(d, tag, n);
    der_put(d, s, n);
}

/* BIT STRING with no unused bits. */
static void
der_bits(der_t *d, const void *s, size_t n)
{
    static const uint8_t zero = 0;
    der_head(d, MBEDTLS_ASN1_BIT_STRING, n + 1);
    der_put(d, &zero, 1);
    der_put(d, s, n);
}

static void
der_small_int(der_t *d, uint8_t v)
{
    der_raw(d, MBEDTLS_ASN1_INTEGER, &v, 1);
}

static void
der_mpi(der_t *d, const mbedtls_mpi *x)
{
    size_t n = mbedtls_mpi_size(x);
    uint8_t *buf = malloc(n + 1);
    if (!buf) {
        d->bad = true;
        return;
    }
    buf[0] = 0;
    if (n && mbedtls_mpi_write_binary(x, buf + 1, n) != 0) d->bad = true;
    size_t skip = (n && !(buf[1] & 0x80)) ? 1 : 0;
    der_raw(d, MBEDTLS_ASN1_INTEGER, buf + skip, n + 1 - skip);
    mbedtls_platform_zeroize(buf, n + 1);
    free(buf);
}

/* AlgorithmIdentifier for the key's type. */
static void
der_alg(der_t *d, const NsPkey *k)
{
    der_t a = { 0 };
    switch (k->type) {
    case NS_PKEY_RSA:
        der_raw(&a, MBEDTLS_ASN1_OID, oid_rsa.p, oid_rsa.len);
        der_raw(&a, MBEDTLS_ASN1_NULL, NULL, 0);
        break;
    case NS_PKEY_EC: {
        const oid_t *c = curve_oid(k->grp.id);
        der_raw(&a, MBEDTLS_ASN1_OID, oid_ec.p, oid_ec.len);
        if (c) der_raw(&a, MBEDTLS_ASN1_OID, c->p, c->len);
        else a.bad = true;
        break;
    }
    case NS_PKEY_X25519:
        der_raw(&a, MBEDTLS_ASN1_OID, oid_x25519.p, oid_x25519.len);
        break;
    case NS_PKEY_X448:
        der_raw(&a, MBEDTLS_ASN1_OID, oid_x448.p, oid_x448.len);
        break;
    default:
        a.bad = true;
        break;
    }
    der_wrap(d, SEQ, &a);
}

/* Hands the finished DER over as a malloc() buffer. */
static uint8_t *
der_take(der_t *d, size_t *len)
{
    if (d->bad) {
        der_free(d);
        return NULL;
    }
    uint8_t *out = copy_out(d->p, d->len);
    if (out) *len = d->len;
    der_free(d);
    return out;
}

uint8_t *
ns_pkey_to_spki(const NsPkey *key, size_t *len)
{
    der_t body = { 0 }, spki = { 0 };
    NsPkey *k = pkey_lock(key);
    der_alg(&body, k);
    if (k->type == NS_PKEY_RSA) {
        der_t rsa = { 0 }, seq = { 0 };
        mbedtls_mpi n, e;
        mbedtls_mpi_init(&n);
        mbedtls_mpi_init(&e);
        if (!rsa_get(&k->rsa, NS_RSA_N, &n) || !rsa_get(&k->rsa, NS_RSA_E, &e))
            body.bad = true;
        der_mpi(&rsa, &n);
        der_mpi(&rsa, &e);
        der_wrap(&seq, SEQ, &rsa);
        der_bits(&body, seq.p, seq.len);
        if (seq.bad) body.bad = true;
        der_free(&seq);
        mbedtls_mpi_free(&n);
        mbedtls_mpi_free(&e);
    } else if (k->type == NS_PKEY_EC) {
        uint8_t pt[MBEDTLS_ECP_MAX_PT_LEN];
        size_t n = 0;
        if (ec_point_write(k, pt, &n)) der_bits(&body, pt, n);
        else body.bad = true;
    } else {
        der_bits(&body, k->okp_pub, k->okp_len);
    }
    pkey_unlock(k);
    der_wrap(&spki, SEQ, &body);
    return der_take(&spki, len);
}

uint8_t *
ns_pkey_to_pkcs8(const NsPkey *key, size_t *len)
{
    if (!key->has_private) return NULL;
    der_t body = { 0 }, inner = { 0 }, p8 = { 0 };
    NsPkey *k = pkey_lock(key);
    der_small_int(&body, 0);
    der_alg(&body, k);
    if (k->type == NS_PKEY_RSA) {
        static const NsRsaPart order[8] = {
            NS_RSA_N, NS_RSA_E, NS_RSA_D, NS_RSA_P, NS_RSA_Q,
            NS_RSA_DP, NS_RSA_DQ, NS_RSA_QI,
        };
        der_t rsa = { 0 };
        der_small_int(&rsa, 0);
        for (int i = 0; i < 8; i++) {
            mbedtls_mpi x;
            mbedtls_mpi_init(&x);
            if (!rsa_get(&k->rsa, order[i], &x)) rsa.bad = true;
            der_mpi(&rsa, &x);
            mbedtls_mpi_free(&x);
        }
        der_wrap(&inner, SEQ, &rsa);
    } else if (k->type == NS_PKEY_EC) {
        /* As OpenSSL writes it: the scalar padded to the order's length,
         * no curve parameters, the public point. */
        der_t ec = { 0 }, pub = { 0 };
        uint8_t dbuf[MBEDTLS_ECP_MAX_BYTES], pt[MBEDTLS_ECP_MAX_PT_LEN];
        size_t dn = (k->grp.nbits + 7) / 8, n = 0;
        der_small_int(&ec, 1);
        if (dn > sizeof dbuf || mbedtls_mpi_write_binary(&k->d, dbuf, dn) != 0)
            ec.bad = true;
        else
            der_raw(&ec, MBEDTLS_ASN1_OCTET_STRING, dbuf, dn);
        mbedtls_platform_zeroize(dbuf, sizeof dbuf);
        if (ec_point_write(k, pt, &n)) der_bits(&pub, pt, n);
        else pub.bad = true;
        der_wrap(&ec, MBEDTLS_ASN1_CONTEXT_SPECIFIC | MBEDTLS_ASN1_CONSTRUCTED | 1,
                 &pub);
        der_wrap(&inner, SEQ, &ec);
    } else {
        der_raw(&inner, MBEDTLS_ASN1_OCTET_STRING, k->okp_priv, k->okp_len);
    }
    pkey_unlock(k);
    der_wrap(&body, MBEDTLS_ASN1_OCTET_STRING, &inner);
    der_wrap(&p8, SEQ, &body);
    return der_take(&p8, len);
}

/* ---- Public-key operations ---- */

bool
ns_pkey_eddsa_context_supported(void)
{
    return false;
}

/* RSA sign or verify with PKCS#1 v1.5 or PSS over the message's hash. */
static int
rsa_sig(NsPkey *k, const NsSigParams *p, bool sign, const uint8_t *hash,
        size_t hlen, uint8_t *sig)
{
    mbedtls_md_type_t md = md_type(p->hash);
    int ret;
    if (p->scheme == NS_SIG_RSA_PKCS1) {
        ret = mbedtls_rsa_set_padding(&k->rsa, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_NONE);
        if (ret) return ret;
        return sign ? mbedtls_rsa_rsassa_pkcs1_v15_sign(&k->rsa, rng, NULL, md,
                                                        (unsigned)hlen, hash, sig)
                    : mbedtls_rsa_rsassa_pkcs1_v15_verify(&k->rsa, md,
                                                          (unsigned)hlen, hash, sig);
    }
    if (p->pss_salt_len > INT_MAX) return MBEDTLS_ERR_RSA_BAD_INPUT_DATA;
    ret = mbedtls_rsa_set_padding(&k->rsa, MBEDTLS_RSA_PKCS_V21, md);
    if (ret) return ret;
    return sign ? mbedtls_rsa_rsassa_pss_sign_ext(&k->rsa, rng, NULL, md,
                                                  (unsigned)hlen, hash,
                                                  (int)p->pss_salt_len, sig)
                : mbedtls_rsa_rsassa_pss_verify_ext(&k->rsa, md, (unsigned)hlen,
                                                    hash, md, (int)p->pss_salt_len,
                                                    sig);
}

/* The width of an EC key's r and s values. */
static size_t
ec_scalar_len(const NsPkey *k)
{
    return (k->grp.nbits + 7) / 8;
}

uint8_t *
ns_pkey_sign(const NsPkey *key, const NsSigParams *p, const uint8_t *data,
             size_t len, size_t *sig_len)
{
    uint8_t hash[NS_HASH_MAX_SIZE];
    size_t hlen = ns_hash_size(p->hash);
    if (p->scheme == NS_SIG_EDDSA || !key->has_private ||
        (key->type != NS_PKEY_RSA && key->type != NS_PKEY_EC) ||
        (p->scheme == NS_SIG_ECDSA) != (key->type == NS_PKEY_EC) ||
        !ns_hash(p->hash, data, len, hash))
        return NULL;
    NsPkey *k = pkey_lock(key);
    uint8_t *out = NULL;
    if (k->type == NS_PKEY_RSA) {
        size_t n = mbedtls_rsa_get_len(&k->rsa);
        out = malloc(n ? n : 1);
        if (out && ok_ret(rsa_sig(k, p, true, hash, hlen, out))) *sig_len = n;
        else { free(out); out = NULL; }
    } else {
        size_t n = ec_scalar_len(k);
        mbedtls_mpi r, s;
        mbedtls_mpi_init(&r);
        mbedtls_mpi_init(&s);
        out = malloc(2 * n);
        if (out && ok_ret(mbedtls_ecdsa_sign(&k->grp, &r, &s, &k->d, hash, hlen,
                                             rng, NULL)) &&
            ok_ret(mbedtls_mpi_write_binary(&r, out, n)) &&
            ok_ret(mbedtls_mpi_write_binary(&s, out + n, n))) {
            *sig_len = 2 * n;
        } else {
            free(out);
            out = NULL;
        }
        mbedtls_mpi_free(&r);
        mbedtls_mpi_free(&s);
    }
    pkey_unlock(k);
    return out;
}

int
ns_pkey_verify(const NsPkey *key, const NsSigParams *p, const uint8_t *sig,
               size_t sig_len, const uint8_t *data, size_t len)
{
    uint8_t hash[NS_HASH_MAX_SIZE];
    size_t hlen = ns_hash_size(p->hash);
    if (p->scheme == NS_SIG_EDDSA ||
        (key->type != NS_PKEY_RSA && key->type != NS_PKEY_EC) ||
        (p->scheme == NS_SIG_ECDSA) != (key->type == NS_PKEY_EC) ||
        !ns_hash(p->hash, data, len, hash))
        return -1;
    NsPkey *k = pkey_lock(key);
    int rc = 0;
    if (k->type == NS_PKEY_RSA) {
        rc = sig_len == mbedtls_rsa_get_len(&k->rsa) &&
             rsa_sig(k, p, false, hash, hlen, (uint8_t *)sig) == 0;
    } else {
        size_t n = ec_scalar_len(k);
        mbedtls_mpi r, s;
        mbedtls_mpi_init(&r);
        mbedtls_mpi_init(&s);
        rc = sig_len == 2 * n &&
             mbedtls_mpi_read_binary(&r, sig, n) == 0 &&
             mbedtls_mpi_read_binary(&s, sig + n, n) == 0 &&
             mbedtls_ecdsa_verify(&k->grp, hash, hlen, &k->Q, &r, &s) == 0;
        mbedtls_mpi_free(&r);
        mbedtls_mpi_free(&s);
    }
    pkey_unlock(k);
    return rc;
}

uint8_t *
ns_pkey_rsa_oaep(const NsPkey *key, bool encrypt, NsHash hash,
                 const uint8_t *label, size_t label_len, const uint8_t *in,
                 size_t len, size_t *out_len)
{
    if (key->type != NS_PKEY_RSA || (!encrypt && !key->has_private) ||
        !md_info(hash))
        return NULL;
    NsPkey *k = pkey_lock(key);
    size_t n = mbedtls_rsa_get_len(&k->rsa);
    uint8_t *out = malloc(n ? n : 1);
    size_t olen = 0;
    bool ok = out && ok_ret(mbedtls_rsa_set_padding(&k->rsa, MBEDTLS_RSA_PKCS_V21,
                                                    md_type(hash)));
    if (ok && encrypt) {
        ok = ok_ret(mbedtls_rsa_rsaes_oaep_encrypt(&k->rsa, rng, NULL, label,
                                                   label_len, len, in, out));
        olen = n;
    } else if (ok) {
        ok = len == n &&
             ok_ret(mbedtls_rsa_rsaes_oaep_decrypt(&k->rsa, rng, NULL, label,
                                                   label_len, &olen, in, out, n));
    }
    pkey_unlock(k);
    if (!ok) {
        if (out) mbedtls_platform_zeroize(out, n ? n : 1);
        free(out);
        return NULL;
    }
    *out_len = olen;
    return out;
}

uint8_t *
ns_pkey_derive(const NsPkey *key, const NsPkey *peer, size_t *len)
{
    if (!key->has_private || key->type != peer->type ||
        (key->type != NS_PKEY_EC && !xdh_len(key->type)) ||
        (key->type == NS_PKEY_EC && key->grp.id != peer->grp.id))
        return NULL;
    mbedtls_mpi z, d;
    mbedtls_ecp_point Q;
    mbedtls_mpi_init(&z);
    mbedtls_mpi_init(&d);
    mbedtls_ecp_point_init(&Q);
    bool ok = true;
    if (key->type == NS_PKEY_EC) {
        /* The peer's point is copied under its lock before this key's is
         * taken, so two threads deriving with a pair never wait on each
         * other. */
        NsPkey *pk = pkey_lock(peer);
        ok = ok_ret(mbedtls_ecp_copy(&Q, &pk->Q));
        pkey_unlock(pk);
    }
    NsPkey *k = pkey_lock(key);
    size_t n = xdh_len(k->type) ? k->okp_len : (k->grp.pbits + 7) / 8;
    uint8_t *out = malloc(n);
    ok = ok && out != NULL;
    if (ok && xdh_len(k->type)) {
        ok = ok_ret(xdh_scalar(k, &d)) &&
             ok_ret(mbedtls_ecp_point_read_binary(&k->grp, &Q, peer->okp_pub,
                                                  peer->okp_len)) &&
             ok_ret(mbedtls_ecdh_compute_shared(&k->grp, &z, &Q, &d, rng, NULL)) &&
             ok_ret(mbedtls_mpi_write_binary_le(&z, out, n));
    } else if (ok) {
        ok = ok_ret(mbedtls_ecdh_compute_shared(&k->grp, &z, &Q, &k->d,
                                                      rng, NULL)) &&
             ok_ret(mbedtls_mpi_write_binary(&z, out, n));
    }
    pkey_unlock(k);
    mbedtls_mpi_free(&z);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_point_free(&Q);
    if (!ok) {
        if (out) mbedtls_platform_zeroize(out, n);
        free(out);
        return NULL;
    }
    *len = n;
    return out;
}
