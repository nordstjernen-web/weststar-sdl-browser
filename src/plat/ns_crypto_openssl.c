/* Weststar SDL Browser — the platform layer: cryptography over OpenSSL's
 * libcrypto (-Dcrypto=openssl, the default). See ns_crypto.h.
 *
 * An NsPkey is an EVP_PKEY. EC keys are set to export their point
 * uncompressed.
 *
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "plat/ns_crypto.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "plat/ns_sdl.h"

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/param_build.h>
#include <openssl/params.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#ifndef OSSL_SIGNATURE_PARAM_CONTEXT_STRING
#define OSSL_SIGNATURE_PARAM_CONTEXT_STRING "context-string"
#endif

static EVP_PKEY *
pk(const NsPkey *key)
{
    return (EVP_PKEY *)key;
}

/* A malloc() copy of len bytes (at least one byte allocated). */
static uint8_t *
copy_out(const void *data, size_t len)
{
    uint8_t *out = malloc(len ? len : 1);
    if (out && len) memcpy(out, data, len);
    return out;
}

/* ---- The library ---- */

const char *
ns_crypto_library_version(void)
{
    return OpenSSL_version(OPENSSL_VERSION);
}

bool
ns_crypto_random(void *buf, size_t len)
{
    unsigned char *p = buf;
    while (len) {
        int n = len > INT_MAX ? INT_MAX : (int)len;
        if (RAND_bytes(p, n) != 1) return false;
        p += n;
        len -= (size_t)n;
    }
    return true;
}

bool
ns_crypto_error_take(char *buf, size_t size)
{
    unsigned long e = ERR_peek_last_error();
    if (e && buf && size) ERR_error_string_n(e, buf, size);
    ERR_clear_error();
    return e != 0;
}

void
ns_crypto_error_clear(void)
{
    ERR_clear_error();
}

void
ns_crypto_cleanse(void *buf, size_t len)
{
    if (buf && len) OPENSSL_cleanse(buf, len);
}

bool
ns_crypto_memeq(const void *a, const void *b, size_t len)
{
    return CRYPTO_memcmp(a, b, len) == 0;
}

/* ---- Hashes, HMAC and key derivation ---- */

static const EVP_MD *
md_of(NsHash h)
{
    switch (h) {
    case NS_HASH_SHA1:     return EVP_sha1();
    case NS_HASH_SHA256:   return EVP_sha256();
    case NS_HASH_SHA384:   return EVP_sha384();
    case NS_HASH_SHA512:   return EVP_sha512();
    case NS_HASH_SHA3_256: return EVP_sha3_256();
    case NS_HASH_SHA3_384: return EVP_sha3_384();
    case NS_HASH_SHA3_512: return EVP_sha3_512();
    }
    return NULL;
}

/* OpenSSL's name for the digest, as EVP_MAC and EVP_KDF take it. */
static const char *
md_name(NsHash h)
{
    switch (h) {
    case NS_HASH_SHA1:     return "SHA1";
    case NS_HASH_SHA256:   return "SHA256";
    case NS_HASH_SHA384:   return "SHA384";
    case NS_HASH_SHA512:   return "SHA512";
    case NS_HASH_SHA3_256: return "SHA3-256";
    case NS_HASH_SHA3_384: return "SHA3-384";
    case NS_HASH_SHA3_512: return "SHA3-512";
    }
    return NULL;
}

bool
ns_hash_available(NsHash h)
{
    return md_of(h) != NULL;
}

bool
ns_hash(NsHash h, const void *data, size_t len, uint8_t *out)
{
    NsHashCtx *ctx = ns_hash_new(h);
    if (!ctx) return false;
    if (!ns_hash_update(ctx, data, len)) {
        EVP_MD_CTX_free((EVP_MD_CTX *)ctx);
        return false;
    }
    return ns_hash_finish(ctx, out);
}

NsHashCtx *
ns_hash_new(NsHash h)
{
    const EVP_MD *md = md_of(h);
    EVP_MD_CTX *ctx = md ? EVP_MD_CTX_new() : NULL;
    if (ctx && EVP_DigestInit_ex(ctx, md, NULL) != 1) {
        EVP_MD_CTX_free(ctx);
        ctx = NULL;
    }
    return (NsHashCtx *)ctx;
}

bool
ns_hash_update(NsHashCtx *ctx, const void *data, size_t len)
{
    return EVP_DigestUpdate((EVP_MD_CTX *)ctx, data ? data : "", len) == 1;
}

bool
ns_hash_finish(NsHashCtx *ctx, uint8_t *out)
{
    unsigned int n = 0;
    bool ok = EVP_DigestFinal_ex((EVP_MD_CTX *)ctx, out, &n) == 1;
    EVP_MD_CTX_free((EVP_MD_CTX *)ctx);
    return ok;
}

bool
ns_hmac(NsHash h, const uint8_t *key, size_t key_len, const uint8_t *data,
        size_t len, uint8_t *out)
{
    EVP_MAC *mac = EVP_MAC_fetch(NULL, "HMAC", NULL);
    EVP_MAC_CTX *ctx = mac ? EVP_MAC_CTX_new(mac) : NULL;
    OSSL_PARAM params[2] = {
        OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST,
                                         (char *)md_name(h), 0),
        OSSL_PARAM_construct_end(),
    };
    size_t n = 0;
    bool ok = ctx && EVP_MAC_init(ctx, key, key_len, params) &&
              EVP_MAC_update(ctx, data, len) &&
              EVP_MAC_final(ctx, out, &n, ns_hash_size(h)) &&
              n == ns_hash_size(h);
    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(mac);
    return ok;
}

bool
ns_pbkdf2(NsHash h, const uint8_t *password, size_t password_len,
          const uint8_t *salt, size_t salt_len, uint32_t iterations,
          uint8_t *out, size_t out_len)
{
    const EVP_MD *md = md_of(h);
    if (!md || password_len > INT_MAX || salt_len > INT_MAX ||
        iterations > INT_MAX || out_len > INT_MAX)
        return false;
    return PKCS5_PBKDF2_HMAC((const char *)password, (int)password_len, salt,
                             (int)salt_len, (int)iterations, md, (int)out_len,
                             out) == 1;
}

bool
ns_hkdf(NsHash h, const uint8_t *ikm, size_t ikm_len, const uint8_t *salt,
        size_t salt_len, const uint8_t *info, size_t info_len, uint8_t *out,
        size_t out_len)
{
    EVP_KDF *kdf = EVP_KDF_fetch(NULL, "HKDF", NULL);
    EVP_KDF_CTX *ctx = kdf ? EVP_KDF_CTX_new(kdf) : NULL;
    OSSL_PARAM params[5];
    int i = 0;
    params[i++] = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST,
                                                   (char *)md_name(h), 0);
    params[i++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY,
                                                    (void *)ikm, ikm_len);
    params[i++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,
                                                    (void *)(salt ? salt : (const uint8_t *)""),
                                                    salt_len);
    params[i++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO,
                                                    (void *)(info ? info : (const uint8_t *)""),
                                                    info_len);
    params[i] = OSSL_PARAM_construct_end();
    bool ok = ctx && EVP_KDF_derive(ctx, out, out_len, params) > 0;
    EVP_KDF_CTX_free(ctx);
    EVP_KDF_free(kdf);
    return ok;
}

/* ---- AES ---- */

typedef enum { AES_CBC, AES_CTR, AES_GCM, AES_KW } aes_mode;

static const EVP_CIPHER *
aes_cipher(aes_mode mode, size_t key_len)
{
    if (key_len != 16 && key_len != 24 && key_len != 32) return NULL;
    switch (mode) {
    case AES_GCM:
        return key_len == 16 ? EVP_aes_128_gcm()
             : key_len == 24 ? EVP_aes_192_gcm() : EVP_aes_256_gcm();
    case AES_CBC:
        return key_len == 16 ? EVP_aes_128_cbc()
             : key_len == 24 ? EVP_aes_192_cbc() : EVP_aes_256_cbc();
    case AES_CTR:
        return key_len == 16 ? EVP_aes_128_ctr()
             : key_len == 24 ? EVP_aes_192_ctr() : EVP_aes_256_ctr();
    case AES_KW:
        return key_len == 16 ? EVP_aes_128_wrap()
             : key_len == 24 ? EVP_aes_192_wrap() : EVP_aes_256_wrap();
    }
    return NULL;
}

/* One EVP pass of a CBC, CTR or KW cipher: init, update, final. */
static bool
aes_pass(aes_mode mode, bool enc, const uint8_t *key, size_t key_len,
         const uint8_t *iv, const uint8_t *in, size_t len, uint8_t *out,
         size_t *out_len)
{
    const EVP_CIPHER *cipher = aes_cipher(mode, key_len);
    if (!cipher || len > (size_t)INT_MAX - 64) return false;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int outl = 0, finl = 0;
    if (!ctx) return false;
    if (mode == AES_KW) EVP_CIPHER_CTX_set_flags(ctx, EVP_CIPHER_CTX_FLAG_WRAP_ALLOW);
    bool ok = EVP_CipherInit_ex(ctx, cipher, NULL, NULL, NULL, enc) &&
              EVP_CipherInit_ex(ctx, NULL, NULL, key, iv, enc) &&
              (!len || EVP_CipherUpdate(ctx, out, &outl, in, (int)len)) &&
              EVP_CipherFinal_ex(ctx, out + outl, &finl);
    EVP_CIPHER_CTX_free(ctx);
    if (ok && out_len) *out_len = (size_t)outl + (size_t)finl;
    return ok;
}

bool
ns_aes_cbc(bool encrypt, const uint8_t *key, size_t key_len,
           const uint8_t iv[16], const uint8_t *in, size_t len, uint8_t *out,
           size_t *out_len)
{
    return aes_pass(AES_CBC, encrypt, key, key_len, iv, in, len, out, out_len);
}

bool
ns_aes_ctr(const uint8_t *key, size_t key_len, const uint8_t counter[16],
           const uint8_t *in, size_t len, uint8_t *out)
{
    return aes_pass(AES_CTR, true, key, key_len, counter, in, len, out, NULL);
}

bool
ns_aes_kw_wrap(const uint8_t *key, size_t key_len, const uint8_t *in,
               size_t len, uint8_t *out)
{
    size_t n = 0;
    return aes_pass(AES_KW, true, key, key_len, NULL, in, len, out, &n) &&
           n == len + 8;
}

bool
ns_aes_kw_unwrap(const uint8_t *key, size_t key_len, const uint8_t *in,
                 size_t len, uint8_t *out)
{
    size_t n = 0;
    return len >= 8 &&
           aes_pass(AES_KW, false, key, key_len, NULL, in, len, out, &n) &&
           n == len - 8;
}

/* GCM in either direction; tag is written (enc) or checked (!enc). */
static bool
aes_gcm(bool enc, const uint8_t *key, size_t key_len, const uint8_t *iv,
        size_t iv_len, const uint8_t *aad, size_t aad_len, const uint8_t *in,
        size_t len, uint8_t *out, uint8_t *tag, size_t tag_len)
{
    const EVP_CIPHER *cipher = aes_cipher(AES_GCM, key_len);
    if (!cipher || len > (size_t)INT_MAX - 64 || aad_len > INT_MAX ||
        iv_len > INT_MAX || tag_len > 16)
        return false;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int outl = 0, finl = 0;
    if (!ctx) return false;
    bool ok = EVP_CipherInit_ex(ctx, cipher, NULL, NULL, NULL, enc) &&
              EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)iv_len, NULL) &&
              EVP_CipherInit_ex(ctx, NULL, NULL, key, iv, enc) &&
              (!aad_len || EVP_CipherUpdate(ctx, NULL, &outl, aad, (int)aad_len));
    outl = 0;
    ok = ok && (!len || EVP_CipherUpdate(ctx, out, &outl, in, (int)len)) &&
         (enc || EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, (int)tag_len, tag)) &&
         EVP_CipherFinal_ex(ctx, out + outl, &finl) &&
         (!enc || EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, (int)tag_len, tag));
    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

bool
ns_aes_gcm_encrypt(const uint8_t *key, size_t key_len, const uint8_t *iv,
                   size_t iv_len, const uint8_t *aad, size_t aad_len,
                   const uint8_t *in, size_t len, uint8_t *out, uint8_t *tag,
                   size_t tag_len)
{
    return aes_gcm(true, key, key_len, iv, iv_len, aad, aad_len, in, len, out,
                   tag, tag_len);
}

bool
ns_aes_gcm_decrypt(const uint8_t *key, size_t key_len, const uint8_t *iv,
                   size_t iv_len, const uint8_t *aad, size_t aad_len,
                   const uint8_t *in, size_t len, const uint8_t *tag,
                   size_t tag_len, uint8_t *out)
{
    uint8_t tagbuf[16];
    if (tag_len > sizeof tagbuf) return false;
    memcpy(tagbuf, tag, tag_len);
    return aes_gcm(false, key, key_len, iv, iv_len, aad, aad_len, in, len, out,
                   tagbuf, tag_len);
}

/* ---- Public-key pairs ---- */

static const char *
okp_name(NsPkeyType type)
{
    switch (type) {
    case NS_PKEY_ED25519: return "ED25519";
    case NS_PKEY_X25519:  return "X25519";
    case NS_PKEY_ED448:   return "ED448";
    case NS_PKEY_X448:    return "X448";
    default:              return NULL;
    }
}

static const char *
curve_group(NsCurve curve)
{
    switch (curve) {
    case NS_CURVE_P256: return "prime256v1";
    case NS_CURVE_P384: return "secp384r1";
    case NS_CURVE_P521: return "secp521r1";
    default:            return NULL;
    }
}

/* Byte length of a curve's field elements: 32, 48 or 66. */
static int
curve_bytes(NsCurve curve)
{
    switch (curve) {
    case NS_CURVE_P256: return 32;
    case NS_CURVE_P384: return 48;
    case NS_CURVE_P521: return 66;
    default:            return 0;
    }
}

/* Forces uncompressed point encoding on an EC key for export. */
static NsPkey *
finish_key(EVP_PKEY *pkey)
{
    if (pkey && EVP_PKEY_is_a(pkey, "EC"))
        EVP_PKEY_set_utf8_string_param(pkey,
            OSSL_PKEY_PARAM_EC_POINT_CONVERSION_FORMAT,
            OSSL_PKEY_EC_POINT_CONVERSION_FORMAT_UNCOMPRESSED);
    return (NsPkey *)pkey;
}

bool
ns_pkey_type_available(NsPkeyType type)
{
    (void)type;
    return true;
}

void
ns_pkey_free(NsPkey *key)
{
    EVP_PKEY_free(pk(key));
}

NsPkey *
ns_pkey_dup(const NsPkey *key)
{
    return (NsPkey *)EVP_PKEY_dup(pk(key));
}

/* The type, or -1 for a key of a kind Weststar does not use. */
static int
pkey_type_of(const EVP_PKEY *pkey)
{
    if (EVP_PKEY_is_a(pkey, "RSA")) return NS_PKEY_RSA;
    if (EVP_PKEY_is_a(pkey, "EC")) return NS_PKEY_EC;
    if (EVP_PKEY_is_a(pkey, "ED25519")) return NS_PKEY_ED25519;
    if (EVP_PKEY_is_a(pkey, "X25519")) return NS_PKEY_X25519;
    if (EVP_PKEY_is_a(pkey, "ED448")) return NS_PKEY_ED448;
    if (EVP_PKEY_is_a(pkey, "X448")) return NS_PKEY_X448;
    return -1;
}

NsPkeyType
ns_pkey_type(const NsPkey *key)
{
    return (NsPkeyType)pkey_type_of(pk(key));
}

NsCurve
ns_pkey_curve(const NsPkey *key)
{
    static const struct { const char *group; NsCurve curve; } curves[] = {
        { "prime256v1", NS_CURVE_P256 },
        { "secp384r1", NS_CURVE_P384 },
        { "secp521r1", NS_CURVE_P521 },
    };
    char group[64];
    if (!key || !EVP_PKEY_is_a(pk(key), "EC") ||
        EVP_PKEY_get_utf8_string_param(pk(key), OSSL_PKEY_PARAM_GROUP_NAME,
                                       group, sizeof group, NULL) <= 0)
        return NS_CURVE_NONE;
    for (size_t i = 0; i < sizeof curves / sizeof curves[0]; i++)
        if (!SDL_strcasecmp(group, curves[i].group)) return curves[i].curve;
    return NS_CURVE_NONE;
}

int
ns_pkey_bits(const NsPkey *key)
{
    return EVP_PKEY_get_bits(pk(key));
}

/* Runs keygen on a context made by the caller, then frees it. */
static NsPkey *
generate(EVP_PKEY_CTX *ctx, bool ok)
{
    EVP_PKEY *pkey = NULL;
    if (!ctx || !ok || EVP_PKEY_generate(ctx, &pkey) <= 0) pkey = NULL;
    EVP_PKEY_CTX_free(ctx);
    return finish_key(pkey);
}

NsPkey *
ns_pkey_generate_rsa(int modulus_bits, uint32_t public_exponent)
{
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(NULL, "RSA", NULL);
    BIGNUM *e = BN_new();
    if (!e || !BN_set_word(e, public_exponent ? public_exponent : 65537)) {
        BN_free(e);
        EVP_PKEY_CTX_free(ctx);
        return NULL;
    }
    NsPkey *key = generate(ctx, ctx && EVP_PKEY_keygen_init(ctx) > 0 &&
                           EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, modulus_bits) > 0 &&
                           EVP_PKEY_CTX_set1_rsa_keygen_pubexp(ctx, e) > 0);
    BN_free(e);
    return key;
}

NsPkey *
ns_pkey_generate_ec(NsCurve curve)
{
    const char *group = curve_group(curve);
    if (!group) return NULL;
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
    return generate(ctx, ctx && EVP_PKEY_keygen_init(ctx) > 0 &&
                    EVP_PKEY_CTX_set_group_name(ctx, group) > 0);
}

NsPkey *
ns_pkey_generate_okp(NsPkeyType type)
{
    const char *name = okp_name(type);
    if (!name) return NULL;
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(NULL, name, NULL);
    return generate(ctx, ctx && EVP_PKEY_keygen_init(ctx) > 0);
}

/* Keeps a decoded key only if it is of a type Weststar uses. */
static NsPkey *
known_key(EVP_PKEY *pkey)
{
    if (pkey && pkey_type_of(pkey) < 0) {
        EVP_PKEY_free(pkey);
        pkey = NULL;
    }
    return finish_key(pkey);
}

NsPkey *
ns_pkey_from_spki(const uint8_t *der, size_t len)
{
    const unsigned char *p = der;
    if (len > LONG_MAX) return NULL;
    EVP_PKEY *pkey = d2i_PUBKEY(NULL, &p, (long)len);
    if (pkey && p != der + len) {
        EVP_PKEY_free(pkey);
        return NULL;
    }
    return known_key(pkey);
}

NsPkey *
ns_pkey_from_pkcs8(const uint8_t *der, size_t len)
{
    const unsigned char *p = der;
    if (len > LONG_MAX) return NULL;
    PKCS8_PRIV_KEY_INFO *p8 = d2i_PKCS8_PRIV_KEY_INFO(NULL, &p, (long)len);
    if (!p8) return NULL;
    EVP_PKEY *pkey = p == der + len ? EVP_PKCS82PKEY(p8) : NULL;
    PKCS8_PRIV_KEY_INFO_free(p8);
    return known_key(pkey);
}

/* Copies an OpenSSL-allocated DER buffer of n bytes to malloc() memory. */
static uint8_t *
take_der(unsigned char *der, int n, size_t *len)
{
    if (n <= 0) return NULL;
    uint8_t *out = copy_out(der, (size_t)n);
    OPENSSL_free(der);
    if (out) *len = (size_t)n;
    return out;
}

uint8_t *
ns_pkey_to_spki(const NsPkey *key, size_t *len)
{
    unsigned char *der = NULL;
    int n = i2d_PUBKEY(pk(key), &der);
    return take_der(der, n, len);
}

uint8_t *
ns_pkey_to_pkcs8(const NsPkey *key, size_t *len)
{
    PKCS8_PRIV_KEY_INFO *p8 = EVP_PKEY2PKCS8(pk(key));
    if (!p8) return NULL;
    unsigned char *der = NULL;
    int n = i2d_PKCS8_PRIV_KEY_INFO(p8, &der);
    PKCS8_PRIV_KEY_INFO_free(p8);
    return take_der(der, n, len);
}

/* Big-endian bytes to a new BIGNUM; NULL for empty input. */
static BIGNUM *
bn_of(const NsBytes *b)
{
    return (b->data && b->len && b->len <= INT_MAX)
               ? BN_bin2bn(b->data, (int)b->len, NULL) : NULL;
}

/* Builds a key of the named type from a parameter list, then frees it. */
static EVP_PKEY *
from_params(const char *type, OSSL_PARAM_BLD *bld, int selection)
{
    OSSL_PARAM *params = bld ? OSSL_PARAM_BLD_to_param(bld) : NULL;
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(NULL, type, NULL);
    EVP_PKEY *pkey = NULL;
    if (!params || !ctx || EVP_PKEY_fromdata_init(ctx) <= 0 ||
        EVP_PKEY_fromdata(ctx, &pkey, selection, params) <= 0)
        pkey = NULL;
    OSSL_PARAM_free(params);
    OSSL_PARAM_BLD_free(bld);
    EVP_PKEY_CTX_free(ctx);
    return pkey;
}

static const char *const rsa_params[NS_RSA_PARTS] = {
    OSSL_PKEY_PARAM_RSA_N, OSSL_PKEY_PARAM_RSA_E, OSSL_PKEY_PARAM_RSA_D,
    OSSL_PKEY_PARAM_RSA_FACTOR1, OSSL_PKEY_PARAM_RSA_FACTOR2,
    OSSL_PKEY_PARAM_RSA_EXPONENT1, OSSL_PKEY_PARAM_RSA_EXPONENT2,
    OSSL_PKEY_PARAM_RSA_COEFFICIENT1,
};

NsPkey *
ns_pkey_from_rsa(const NsBytes parts[NS_RSA_PARTS])
{
    bool private_key = parts[NS_RSA_D].data && parts[NS_RSA_D].len;
    int last = NS_RSA_E;
    if (private_key) {
        last = NS_RSA_D;
        if (parts[NS_RSA_P].data && parts[NS_RSA_Q].data) {
            last = NS_RSA_Q;
            if (parts[NS_RSA_DP].data && parts[NS_RSA_DQ].data &&
                parts[NS_RSA_QI].data)
                last = NS_RSA_QI;
        }
    }
    OSSL_PARAM_BLD *bld = OSSL_PARAM_BLD_new();
    BIGNUM *bn[NS_RSA_PARTS] = { 0 };
    for (int i = 0; i <= last; i++) {
        bn[i] = bn_of(&parts[i]);
        OSSL_PARAM_BLD_push_BN(bld, rsa_params[i], bn[i]);
    }
    EVP_PKEY *pkey = from_params("RSA", bld,
                                 private_key ? EVP_PKEY_KEYPAIR : EVP_PKEY_PUBLIC_KEY);
    for (int i = 0; i < NS_RSA_PARTS; i++) BN_free(bn[i]);
    return (NsPkey *)pkey;
}

/* A BIGNUM key parameter as minimal big-endian bytes. */
static uint8_t *
bn_param(const NsPkey *key, const char *param, size_t *len)
{
    BIGNUM *bn = NULL;
    if (!key || EVP_PKEY_get_bn_param(pk(key), param, &bn) <= 0 || !bn) {
        ERR_clear_error();
        return NULL;
    }
    int n = BN_num_bytes(bn);
    uint8_t *out = malloc(n ? (size_t)n : 1);
    if (out) {
        BN_bn2bin(bn, out);
        *len = (size_t)n;
    }
    BN_clear_free(bn);
    return out;
}

uint8_t *
ns_pkey_rsa_part(const NsPkey *key, NsRsaPart part, size_t *len)
{
    return part < NS_RSA_PARTS ? bn_param(key, rsa_params[part], len) : NULL;
}

/* True if the key pair passes OpenSSL's pairwise consistency check. */
static bool
pairwise_ok(EVP_PKEY *pkey)
{
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_pkey(NULL, pkey, NULL);
    bool ok = ctx && EVP_PKEY_pairwise_check(ctx) > 0;
    EVP_PKEY_CTX_free(ctx);
    ERR_clear_error();
    return ok;
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
    const char *group = curve_group(curve);
    if (!group) return NULL;
    bool private_key = d && d_len;
    OSSL_PARAM_BLD *bld = OSSL_PARAM_BLD_new();
    OSSL_PARAM_BLD_push_utf8_string(bld, OSSL_PKEY_PARAM_GROUP_NAME, group, 0);
    OSSL_PARAM_BLD_push_octet_string(bld, OSSL_PKEY_PARAM_PUB_KEY, point,
                                     point_len);
    BIGNUM *bn_d = NULL;
    if (private_key) {
        NsBytes b = { d, d_len };
        bn_d = bn_of(&b);
        OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_PRIV_KEY, bn_d);
    }
    EVP_PKEY *pkey = from_params("EC", bld,
                                 private_key ? EVP_PKEY_KEYPAIR : EVP_PKEY_PUBLIC_KEY);
    BN_clear_free(bn_d);
    if (pkey && private_key && !pairwise_ok(pkey)) {
        EVP_PKEY_free(pkey);
        pkey = NULL;
    }
    return finish_key(pkey);
}

uint8_t *
ns_pkey_ec_point(const NsPkey *key, size_t *len)
{
    unsigned char *enc = NULL;
    size_t n = EVP_PKEY_get1_encoded_public_key(pk(key), &enc);
    uint8_t *out = n ? copy_out(enc, n) : NULL;
    OPENSSL_free(enc);
    if (out) *len = n;
    return out;
}

uint8_t *
ns_pkey_ec_private(const NsPkey *key, size_t *len)
{
    return bn_param(key, OSSL_PKEY_PARAM_PRIV_KEY, len);
}

NsPkey *
ns_pkey_from_okp(NsPkeyType type, const uint8_t *data, size_t len,
                 bool private_key)
{
    const char *name = okp_name(type);
    if (!name) return NULL;
    return (NsPkey *)(private_key
        ? EVP_PKEY_new_raw_private_key_ex(NULL, name, NULL, data, len)
        : EVP_PKEY_new_raw_public_key_ex(NULL, name, NULL, data, len));
}

uint8_t *
ns_pkey_okp_public(const NsPkey *key, size_t *len)
{
    size_t n = 0;
    if (!key || EVP_PKEY_get_raw_public_key(pk(key), NULL, &n) <= 0 || !n)
        return NULL;
    uint8_t *out = malloc(n);
    if (out && EVP_PKEY_get_raw_public_key(pk(key), out, &n) <= 0) {
        free(out);
        return NULL;
    }
    if (out) *len = n;
    return out;
}

uint8_t *
ns_pkey_okp_private(const NsPkey *key, size_t *len)
{
    size_t n = 0;
    if (!key || EVP_PKEY_get_raw_private_key(pk(key), NULL, &n) <= 0 || !n)
        return NULL;
    uint8_t *out = malloc(n);
    if (out && EVP_PKEY_get_raw_private_key(pk(key), out, &n) <= 0) {
        free(out);
        return NULL;
    }
    if (out) *len = n;
    return out;
}

/* ---- Public-key operations ---- */

bool
ns_pkey_eddsa_context_supported(void)
{
    EVP_SIGNATURE *sig = EVP_SIGNATURE_fetch(NULL, "ED448", NULL);
    bool ok = sig && OSSL_PARAM_locate_const(
        EVP_SIGNATURE_settable_ctx_params(sig),
        OSSL_SIGNATURE_PARAM_CONTEXT_STRING) != NULL;
    EVP_SIGNATURE_free(sig);
    ERR_clear_error();
    return ok;
}

/* Initializes EdDSA sign/verify; a context string is only allowed for Ed448. */
static int
eddsa_init(EVP_MD_CTX *mdctx, const NsPkey *key, const NsSigParams *p, bool sign)
{
    if (!p->context_len)
        return sign ? EVP_DigestSignInit(mdctx, NULL, NULL, NULL, pk(key))
                    : EVP_DigestVerifyInit(mdctx, NULL, NULL, NULL, pk(key));
    if (!EVP_PKEY_is_a(pk(key), "ED448") || p->context_len > 255 ||
        !ns_pkey_eddsa_context_supported())
        return 0;
    OSSL_PARAM params[2] = {
        OSSL_PARAM_construct_octet_string(OSSL_SIGNATURE_PARAM_CONTEXT_STRING,
                                          (void *)p->context, p->context_len),
        OSSL_PARAM_construct_end(),
    };
    return sign ? EVP_DigestSignInit_ex(mdctx, NULL, NULL, NULL, NULL, pk(key),
                                        params)
                : EVP_DigestVerifyInit_ex(mdctx, NULL, NULL, NULL, NULL, pk(key),
                                          params);
}

/* Initializes RSA (PKCS#1 v1.5 or PSS) or ECDSA sign/verify with the hash. */
static int
digest_init(EVP_MD_CTX *mdctx, const NsPkey *key, const NsSigParams *p, bool sign)
{
    const EVP_MD *md = md_of(p->hash);
    EVP_PKEY_CTX *pctx = NULL;
    if (!md) return 0;
    if ((sign ? EVP_DigestSignInit(mdctx, &pctx, md, NULL, pk(key))
              : EVP_DigestVerifyInit(mdctx, &pctx, md, NULL, pk(key))) <= 0)
        return 0;
    if (p->scheme == NS_SIG_RSA_PSS)
        return EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING) > 0 &&
               p->pss_salt_len <= INT_MAX &&
               EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, (int)p->pss_salt_len) > 0;
    if (p->scheme == NS_SIG_RSA_PKCS1)
        return EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PADDING) > 0;
    return 1;
}

/* A DER ECDSA signature as fixed-width r || s. */
static uint8_t *
ecdsa_der_to_raw(const uint8_t *der, size_t der_len, int order, size_t *out_len)
{
    const unsigned char *p = der;
    ECDSA_SIG *sig = d2i_ECDSA_SIG(NULL, &p, (long)der_len);
    if (!sig) return NULL;
    const BIGNUM *r, *s;
    ECDSA_SIG_get0(sig, &r, &s);
    uint8_t *out = r && s ? calloc(2, (size_t)order) : NULL;
    if (out && (BN_bn2binpad(r, out, order) < 0 ||
                BN_bn2binpad(s, out + order, order) < 0)) {
        free(out);
        out = NULL;
    }
    ECDSA_SIG_free(sig);
    if (out) *out_len = (size_t)order * 2;
    return out;
}

/* A fixed-width r || s ECDSA signature as DER; NULL on the wrong length. */
static uint8_t *
ecdsa_raw_to_der(const uint8_t *raw, size_t raw_len, int order, size_t *out_len)
{
    if (!order || raw_len != (size_t)order * 2) return NULL;
    ECDSA_SIG *sig = ECDSA_SIG_new();
    BIGNUM *r = BN_bin2bn(raw, order, NULL);
    BIGNUM *s = BN_bin2bn(raw + order, order, NULL);
    if (!sig || !r || !s) {
        BN_free(r);
        BN_free(s);
        ECDSA_SIG_free(sig);
        return NULL;
    }
    ECDSA_SIG_set0(sig, r, s);
    unsigned char *der = NULL;
    int n = i2d_ECDSA_SIG(sig, &der);
    ECDSA_SIG_free(sig);
    return take_der(der, n, out_len);
}

uint8_t *
ns_pkey_sign(const NsPkey *key, const NsSigParams *p, const uint8_t *data,
             size_t len, size_t *sig_len)
{
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    size_t n = 0;
    if (!mdctx) return NULL;
    int ok = p->scheme == NS_SIG_EDDSA ? eddsa_init(mdctx, key, p, true)
                                       : digest_init(mdctx, key, p, true);
    uint8_t *out = NULL;
    if (ok > 0 && EVP_DigestSign(mdctx, NULL, &n, data, len) > 0) {
        out = malloc(n ? n : 1);
        if (out && EVP_DigestSign(mdctx, out, &n, data, len) <= 0) {
            free(out);
            out = NULL;
        }
    }
    EVP_MD_CTX_free(mdctx);
    if (out && p->scheme == NS_SIG_ECDSA) {
        uint8_t *raw = ecdsa_der_to_raw(out, n, curve_bytes(ns_pkey_curve(key)), &n);
        free(out);
        out = raw;
    }
    if (out) *sig_len = n;
    return out;
}

int
ns_pkey_verify(const NsPkey *key, const NsSigParams *p, const uint8_t *sig,
               size_t sig_len, const uint8_t *data, size_t len)
{
    uint8_t *der = NULL;
    if (p->scheme == NS_SIG_ECDSA) {
        size_t der_len = 0;
        der = ecdsa_raw_to_der(sig, sig_len, curve_bytes(ns_pkey_curve(key)),
                               &der_len);
        if (!der) return -1;
        sig = der;
        sig_len = der_len;
    }
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    int rc = -1;
    if (mdctx && (p->scheme == NS_SIG_EDDSA ? eddsa_init(mdctx, key, p, false)
                                            : digest_init(mdctx, key, p, false)) > 0) {
        int v = EVP_DigestVerify(mdctx, sig, sig_len, data, len);
        rc = v == 1 ? 1 : 0;
        if (v < 0) ERR_clear_error();
    }
    EVP_MD_CTX_free(mdctx);
    free(der);
    return rc;
}

uint8_t *
ns_pkey_rsa_oaep(const NsPkey *key, bool encrypt, NsHash hash,
                 const uint8_t *label, size_t label_len, const uint8_t *in,
                 size_t len, size_t *out_len)
{
    const EVP_MD *md = md_of(hash);
    EVP_PKEY_CTX *ctx = md ? EVP_PKEY_CTX_new_from_pkey(NULL, pk(key), NULL) : NULL;
    uint8_t *out = NULL;
    size_t n = 0;
    int ok = ctx &&
             (encrypt ? EVP_PKEY_encrypt_init(ctx) : EVP_PKEY_decrypt_init(ctx)) > 0 &&
             EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) > 0 &&
             EVP_PKEY_CTX_set_rsa_oaep_md(ctx, md) > 0 &&
             EVP_PKEY_CTX_set_rsa_mgf1_md(ctx, md) > 0;
    if (ok && label && label_len) {
        unsigned char *lbl = label_len <= INT_MAX
                                 ? OPENSSL_memdup(label, label_len) : NULL;
        if (!lbl ||
            EVP_PKEY_CTX_set0_rsa_oaep_label(ctx, lbl, (int)label_len) <= 0) {
            OPENSSL_free(lbl);
            ok = 0;
        }
    }
    if (ok) {
        int rc = encrypt ? EVP_PKEY_encrypt(ctx, NULL, &n, in, len)
                         : EVP_PKEY_decrypt(ctx, NULL, &n, in, len);
        if (rc > 0) {
            out = malloc(n ? n : 1);
            rc = !out ? 0 : encrypt ? EVP_PKEY_encrypt(ctx, out, &n, in, len)
                                    : EVP_PKEY_decrypt(ctx, out, &n, in, len);
            if (rc <= 0) {
                free(out);
                out = NULL;
            }
        }
    }
    EVP_PKEY_CTX_free(ctx);
    if (out) *out_len = n;
    return out;
}

uint8_t *
ns_pkey_derive(const NsPkey *key, const NsPkey *peer, size_t *len)
{
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_pkey(NULL, pk(key), NULL);
    uint8_t *out = NULL;
    size_t n = 0;
    if (ctx && EVP_PKEY_derive_init(ctx) > 0 &&
        EVP_PKEY_derive_set_peer(ctx, pk(peer)) > 0 &&
        EVP_PKEY_derive(ctx, NULL, &n) > 0) {
        size_t cap = n ? n : 1;
        out = malloc(cap);
        if (out && EVP_PKEY_derive(ctx, out, &n) <= 0) {
            OPENSSL_cleanse(out, cap);
            free(out);
            out = NULL;
        }
    }
    EVP_PKEY_CTX_free(ctx);
    if (out) *len = n;
    return out;
}
