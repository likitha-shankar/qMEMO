/*
 * key_inspection.c -- Cryptographic Key Material Inspection
 *
 * Part of the qMEMO project (IIT Chicago).
 *
 * Produces a research-grade audit of all 7 signature schemes:
 *   Falcon-512, Falcon-1024, ML-DSA-44, ML-DSA-65, SLH-DSA-SHA2-128f,
 *   ECDSA secp256k1, Ed25519
 *
 * For each scheme this program:
 *   1. Generates a fresh keypair and reports exact byte sizes.
 *   2. Dumps the full public key in hexdump -C format.
 *      (Secret key bytes are NEVER printed -- only the size is reported.)
 *   3. Signs a fixed 64-byte test vector and dumps the full signature.
 *   4. Runs three correctness checks:
 *        (a) verify original  sig on original  msg → must PASS
 *        (b) verify corrupted sig on original  msg → must FAIL
 *        (c) verify original  sig on corrupted msg → must FAIL
 *   5. Reports single-operation timing (one keygen / one sign / one verify)
 *      as a latency reference -- not for throughput; use the other benchmarks.
 *
 * Test vector (64 bytes, ASCII):
 *   "qMEMO key inspection test vector 2026-02-24 IIT Chicago!!!!!!!!"
 *
 * Output goes to stdout.  Redirect or tee to a .log file:
 *   ./bin/key_inspection | tee results/key_inspection.log
 *
 * Compile:
 *   make key_inspection
 *
 * Run:
 *   ./benchmarks/bin/key_inspection
 */

#include "bench_common.h"       /* get_time, get_timestamp -- must be first */

#include <oqs/oqs.h>
#include <oqs/oqsconfig.h>      /* OQS_VERSION_TEXT */
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/opensslv.h>   /* OPENSSL_VERSION_TEXT */
#include <openssl/x509.h>       /* i2d_PUBKEY */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Configuration ────────────────────────────────────────────────────────── */

/* 64-byte fixed test vector: ASCII-printable, embeds project identity */
static const uint8_t TEST_MSG[64] =
    "qMEMO key inspection test vector 2026-02-24 IIT Chicago!!!!!!!!";
#define TEST_MSG_LEN 64

/*
 * Maximum bytes to display per hex dump.
 * Public keys: show all (max 1793 bytes for Falcon-1024).
 * Signatures: cap large ones at SIG_HEX_MAX; SLH-DSA is 17 KB.
 */
#define PK_HEX_MAX   0     /* 0 = unlimited */
#define SIG_HEX_MAX  512   /* first 512 bytes for oversized sigs */

/* ── Hex dump (hexdump -C format) ─────────────────────────────────────────── */

static void hex_dump(const uint8_t *buf, size_t len, size_t max_show)
{
    size_t show = (max_show == 0 || len <= max_show) ? len : max_show;

    for (size_t i = 0; i < show; i += 16) {
        size_t row_end = (i + 16 < show) ? i + 16 : show;

        printf("    [%04zx]", i);

        /* Hex bytes -- gap after byte 8 */
        for (size_t j = i; j < i + 16; j++) {
            if (j == i + 8) printf(" ");
            if (j < row_end) printf(" %02x", buf[j]);
            else              printf("   ");
        }

        /* ASCII column */
        printf("  |");
        for (size_t j = i; j < row_end; j++)
            putchar((buf[j] >= 0x20 && buf[j] < 0x7f) ? (char)buf[j] : '.');
        printf("|\n");
    }

    if (max_show > 0 && len > max_show)
        printf("    ... [showing %zu of %zu bytes total]\n", show, len);
}

/* ── Section header helpers ───────────────────────────────────────────────── */

static void section_header(int idx, const char *name, int nist_level,
                            const char *type)
{
    printf("\n");
    printf("────────────────────────────────────────────────────────────────\n");
    if (nist_level > 0)
        printf("  [%d/7] %s  (NIST Level %d -- %s)\n", idx, name, nist_level, type);
    else
        printf("  [%d/7] %s  (Classical -- %s)\n", idx, name, type);
    printf("────────────────────────────────────────────────────────────────\n");
}

/* ── OQS inspection ───────────────────────────────────────────────────────── */

static int inspect_oqs(int idx, const char *alg_id, const char *name,
                        int nist_level, const char *type)
{
    section_header(idx, name, nist_level, type);

    OQS_SIG *sig = OQS_SIG_new(alg_id);
    if (!sig) {
        printf("  ERROR: cannot instantiate %s\n", alg_id);
        return -1;
    }

    uint8_t *public_key = malloc(sig->length_public_key);
    uint8_t *secret_key = malloc(sig->length_secret_key);
    uint8_t *signature  = malloc(sig->length_signature);

    if (!public_key || !secret_key || !signature) {
        fprintf(stderr, "  ERROR: malloc failed\n");
        free(public_key); free(secret_key); free(signature);
        OQS_SIG_free(sig);
        return -1;
    }

    /* ── Key sizes ────────────────────────────────────────────────────── */
    printf("\n  Key Sizes:\n");
    printf("    Public key:  %zu bytes\n", sig->length_public_key);
    printf("    Secret key:  %zu bytes  [NOT DISPLAYED -- secret material]\n",
           sig->length_secret_key);
    printf("    Sig buffer:  %zu bytes  (maximum allocation)\n",
           sig->length_signature);

    /* ── Keygen ───────────────────────────────────────────────────────── */
    double t0 = get_time();
    OQS_STATUS rc = OQS_SIG_keypair(sig, public_key, secret_key);
    double keygen_us = (get_time() - t0) * 1e6;

    if (rc != OQS_SUCCESS) {
        printf("  ERROR: keypair generation failed\n");
        goto fail;
    }
    printf("    Keygen time: %.1f µs\n", keygen_us);

    /* ── Public key hex dump ──────────────────────────────────────────── */
    printf("\n  Public Key (%zu bytes):\n", sig->length_public_key);
    hex_dump(public_key, sig->length_public_key, PK_HEX_MAX);

    /* ── Sign ─────────────────────────────────────────────────────────── */
    size_t sig_len = 0;
    t0 = get_time();
    rc = OQS_SIG_sign(sig, signature, &sig_len, TEST_MSG, TEST_MSG_LEN, secret_key);
    double sign_us = (get_time() - t0) * 1e6;

    if (rc != OQS_SUCCESS) {
        printf("  ERROR: signing failed\n");
        goto fail;
    }
    printf("\n  Signature (%zu bytes actual, %zu bytes max):\n",
           sig_len, sig->length_signature);
    printf("    Sign time:  %.1f µs\n", sign_us);
    hex_dump(signature, sig_len, SIG_HEX_MAX);

    /* ── Correctness checks ───────────────────────────────────────────── */
    printf("\n  Correctness Checks:\n");

    /* (a) Correct sig + correct msg → PASS */
    t0 = get_time();
    rc = OQS_SIG_verify(sig, TEST_MSG, TEST_MSG_LEN,
                        signature, sig_len, public_key);
    double verify_us = (get_time() - t0) * 1e6;
    printf("    (a) Verify correct sig / correct msg:  %s  (%.1f µs)\n",
           rc == OQS_SUCCESS ? "PASS ✓" : "FAIL ✗", verify_us);

    /* (b) Flip signature byte 0 → FAIL */
    uint8_t saved = signature[0];
    signature[0] ^= 0xFF;
    rc = OQS_SIG_verify(sig, TEST_MSG, TEST_MSG_LEN,
                        signature, sig_len, public_key);
    signature[0] = saved;
    printf("    (b) Verify corrupted sig  / correct msg:  %s\n",
           rc != OQS_SUCCESS ? "FAIL ✓ (expected)" : "PASS ✗ (UNEXPECTED)");

    /* (c) Correct sig + flip message byte 0 → FAIL */
    uint8_t msg_copy[TEST_MSG_LEN];
    memcpy(msg_copy, TEST_MSG, TEST_MSG_LEN);
    msg_copy[0] ^= 0x01;
    rc = OQS_SIG_verify(sig, msg_copy, TEST_MSG_LEN,
                        signature, sig_len, public_key);
    printf("    (c) Verify correct sig   / corrupted msg: %s\n",
           rc != OQS_SUCCESS ? "FAIL ✓ (expected)" : "PASS ✗ (UNEXPECTED)");

    OQS_MEM_secure_free(secret_key, sig->length_secret_key);
    OQS_MEM_insecure_free(public_key);
    OQS_MEM_insecure_free(signature);
    OQS_SIG_free(sig);
    return 0;

fail:
    OQS_MEM_secure_free(secret_key, sig->length_secret_key);
    OQS_MEM_insecure_free(public_key);
    OQS_MEM_insecure_free(signature);
    OQS_SIG_free(sig);
    return -1;
}

/* ── OpenSSL inspection ───────────────────────────────────────────────────── */

static EVP_PKEY *openssl_keygen(int pkey_type, int curve_nid)
{
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(pkey_type, NULL);
    if (!ctx) return NULL;
    if (EVP_PKEY_keygen_init(ctx) <= 0) { EVP_PKEY_CTX_free(ctx); return NULL; }
    if (curve_nid != 0 &&
        EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, curve_nid) <= 0) {
        EVP_PKEY_CTX_free(ctx); return NULL;
    }
    EVP_PKEY *pkey = NULL;
    if (EVP_PKEY_keygen(ctx, &pkey) <= 0) { EVP_PKEY_CTX_free(ctx); return NULL; }
    EVP_PKEY_CTX_free(ctx);
    return pkey;
}

static size_t openssl_sign(EVP_PKEY *pkey, const EVP_MD *md,
                            const uint8_t *msg, size_t mlen,
                            uint8_t *sig, size_t sig_max)
{
    EVP_MD_CTX *mctx = EVP_MD_CTX_new();
    if (!mctx) return 0;
    if (EVP_DigestSignInit(mctx, NULL, md, NULL, pkey) <= 0) {
        EVP_MD_CTX_free(mctx); return 0;
    }
    size_t sig_len = sig_max;
    if (EVP_DigestSign(mctx, sig, &sig_len, msg, mlen) <= 0) {
        EVP_MD_CTX_free(mctx); return 0;
    }
    EVP_MD_CTX_free(mctx);
    return sig_len;
}

static int openssl_verify(EVP_PKEY *pkey, const EVP_MD *md,
                           const uint8_t *msg, size_t mlen,
                           const uint8_t *sig, size_t sig_len)
{
    EVP_MD_CTX *mctx = EVP_MD_CTX_new();
    if (!mctx) return 0;
    if (EVP_DigestVerifyInit(mctx, NULL, md, NULL, pkey) <= 0) {
        EVP_MD_CTX_free(mctx); return 0;
    }
    int rc = EVP_DigestVerify(mctx, sig, sig_len, msg, mlen);
    EVP_MD_CTX_free(mctx);
    return (rc == 1);
}

static int inspect_openssl(int idx, int pkey_type, int curve_nid,
                             const EVP_MD *md, const char *name,
                             const char *type)
{
    section_header(idx, name, 0, type);

    /* ── Keygen ───────────────────────────────────────────────────────── */
    double t0 = get_time();
    EVP_PKEY *pkey = openssl_keygen(pkey_type, curve_nid);
    double keygen_us = (get_time() - t0) * 1e6;

    if (!pkey) {
        printf("  ERROR: keygen failed\n");
        return -1;
    }

    /* ── Extract public key bytes ─────────────────────────────────────── */
    uint8_t  pk_buf[512];
    size_t   pk_len = 0;
    int      sk_bits = EVP_PKEY_get_bits(pkey);

    if (pkey_type == EVP_PKEY_ED25519) {
        /* Ed25519: 32-byte raw public key */
        pk_len = sizeof(pk_buf);
        EVP_PKEY_get_raw_public_key(pkey, pk_buf, &pk_len);
    } else {
        /* ECDSA: DER-encoded SubjectPublicKeyInfo via i2d_PUBKEY */
        uint8_t *der = NULL;
        int der_len  = i2d_PUBKEY(pkey, &der);
        if (der_len > 0 && (size_t)der_len <= sizeof(pk_buf)) {
            memcpy(pk_buf, der, (size_t)der_len);
            pk_len = (size_t)der_len;
        }
        OPENSSL_free(der);
    }

    /* ── Key sizes ────────────────────────────────────────────────────── */
    int sk_size = (pkey_type == EVP_PKEY_ED25519) ? 32 : (sk_bits / 8);
    printf("\n  Key Sizes:\n");
    printf("    Public key:  %zu bytes%s\n", pk_len,
           (pkey_type != EVP_PKEY_ED25519) ? "  (DER SubjectPublicKeyInfo)" : "  (raw)");
    printf("    Secret key:  %d bytes  [NOT DISPLAYED -- secret material]\n", sk_size);
    printf("    Keygen time: %.1f µs\n", keygen_us);

    /* ── Public key hex dump ──────────────────────────────────────────── */
    if (pk_len > 0) {
        printf("\n  Public Key (%zu bytes):\n", pk_len);
        hex_dump(pk_buf, pk_len, PK_HEX_MAX);
    }

    /* ── Sign ─────────────────────────────────────────────────────────── */
    uint8_t sig_buf[256];
    t0 = get_time();
    size_t sig_len = openssl_sign(pkey, md,
                                   TEST_MSG, TEST_MSG_LEN,
                                   sig_buf, sizeof(sig_buf));
    double sign_us = (get_time() - t0) * 1e6;

    if (!sig_len) {
        printf("  ERROR: signing failed\n");
        EVP_PKEY_free(pkey);
        return -1;
    }

    printf("\n  Signature (%zu bytes):\n", sig_len);
    printf("    Sign time:  %.1f µs\n", sign_us);
    hex_dump(sig_buf, sig_len, PK_HEX_MAX);

    /* ── Correctness checks ───────────────────────────────────────────── */
    printf("\n  Correctness Checks:\n");

    double t_ver = get_time();
    int ok = openssl_verify(pkey, md, TEST_MSG, TEST_MSG_LEN, sig_buf, sig_len);
    double verify_us = (get_time() - t_ver) * 1e6;
    printf("    (a) Verify correct sig / correct msg:  %s  (%.1f µs)\n",
           ok ? "PASS ✓" : "FAIL ✗", verify_us);

    uint8_t saved = sig_buf[4];   /* byte 4: avoids DER header corruption */
    sig_buf[4] ^= 0xFF;
    ok = openssl_verify(pkey, md, TEST_MSG, TEST_MSG_LEN, sig_buf, sig_len);
    sig_buf[4] = saved;
    printf("    (b) Verify corrupted sig  / correct msg:  %s\n",
           !ok ? "FAIL ✓ (expected)" : "PASS ✗ (UNEXPECTED)");

    uint8_t msg_copy[TEST_MSG_LEN];
    memcpy(msg_copy, TEST_MSG, TEST_MSG_LEN);
    msg_copy[0] ^= 0x01;
    ok = openssl_verify(pkey, md, msg_copy, TEST_MSG_LEN, sig_buf, sig_len);
    printf("    (c) Verify correct sig   / corrupted msg: %s\n",
           !ok ? "FAIL ✓ (expected)" : "PASS ✗ (UNEXPECTED)");

    EVP_PKEY_free(pkey);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════ */

int main(void)
{
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    printf("================================================================\n");
    printf("  Cryptographic Key Material Inspection  (qMEMO / IIT Chicago)\n");
    printf("================================================================\n");
    printf("  Generated:  %s\n", timestamp);
    printf("  liboqs:     %s\n", OQS_VERSION_TEXT);
    printf("  OpenSSL:    %s\n", OPENSSL_VERSION_TEXT);
    printf("  Algorithms: 5 PQC (liboqs) + 2 classical (OpenSSL)\n");
    printf("\n");
    printf("  Test vector (%d bytes):\n", TEST_MSG_LEN);
    hex_dump(TEST_MSG, TEST_MSG_LEN, 0);
    printf("\n");
    printf("  NOTE: Secret key bytes are NEVER printed in this output.\n");
    printf("        Only sizes are reported.  Pipe to a log file safely.\n");

    OQS_init();

    int results[7];

    results[0] = inspect_oqs(1, OQS_SIG_alg_falcon_512,  "Falcon-512",  1,
                              "NTRU Lattice -- variable-length sig");
    results[1] = inspect_oqs(2, OQS_SIG_alg_falcon_1024, "Falcon-1024", 5,
                              "NTRU Lattice -- variable-length sig");
    results[2] = inspect_oqs(3, OQS_SIG_alg_ml_dsa_44,   "ML-DSA-44",   2,
                              "Module Lattice (Dilithium)");
    results[3] = inspect_oqs(4, OQS_SIG_alg_ml_dsa_65,   "ML-DSA-65",   3,
                              "Module Lattice (Dilithium)");
    results[4] = inspect_oqs(5, OQS_SIG_alg_slh_dsa_pure_sha2_128f,
                              "SLH-DSA-SHA2-128f", 1,
                              "Hash-based (SPHINCS+) -- fast variant");
    results[5] = inspect_openssl(6, EVP_PKEY_EC, NID_secp256k1, EVP_sha256(),
                                  "ECDSA secp256k1",
                                  "Elliptic Curve -- DER-encoded sig");
    results[6] = inspect_openssl(7, EVP_PKEY_ED25519, 0, NULL,
                                  "Ed25519",
                                  "Edwards Curve -- fixed 64-byte sig");

    OQS_destroy();

    /* ── Summary table ────────────────────────────────────────────────── */
    printf("\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("  Summary\n");
    printf("════════════════════════════════════════════════════════════════\n");
    printf("\n");

    const char *names[7] = {
        "Falcon-512", "Falcon-1024", "ML-DSA-44", "ML-DSA-65",
        "SLH-DSA-SHA2-128f", "ECDSA secp256k1", "Ed25519"
    };
    int nist[7] = { 1, 5, 2, 3, 1, 0, 0 };

    printf("  Algorithm           NIST  Inspected\n");
    printf("  ------------------  ----  ---------\n");
    for (int i = 0; i < 7; i++) {
        const char *level = (nist[i] == 0) ? "   --"
                          : (nist[i] == 1) ? " L1 "
                          : (nist[i] == 2) ? " L2 "
                          : (nist[i] == 3) ? " L3 "
                                           : " L5 ";
        printf("  %-18s  %s  %s\n",
               names[i], level,
               results[i] == 0 ? "PASS -- all correctness checks OK" : "FAILED");
    }

    printf("\n  Test vector: \"%.*s\"\n", TEST_MSG_LEN, (const char *)TEST_MSG);
    printf("  Timestamp:   %s\n", timestamp);
    printf("\n");

    for (int i = 0; i < 7; i++)
        if (results[i] != 0) return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
