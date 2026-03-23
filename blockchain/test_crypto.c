/**
 * test_crypto.c - Round-trip sign/verify test for crypto_backend
 *
 * Build & run:
 *   make test_crypto                  # ECDSA (default)
 *   make test_crypto SIG_SCHEME=2     # Falcon-512
 *   make test_crypto SIG_SCHEME=3     # Hybrid (tests both backends)
 *   make test_crypto SIG_SCHEME=4     # ML-DSA-44
 */
#include "crypto_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/**
 * Run keygen → sign → verify → tamper tests for a single scheme.
 * Returns 0 on success, 1 on failure.
 */
static int test_scheme(uint8_t sig_type, const char *label) {
    printf("--- %s backend ---\n", label);

    crypto_ctx_t *ctx = crypto_ctx_new(sig_type);
    if (!ctx) {
        printf("[FAIL] crypto_ctx_new(%s) returned NULL\n", label);
        return 1;
    }

    uint8_t pubkey[CRYPTO_PUBKEY_MAX];
    uint8_t seckey[CRYPTO_SECKEY_MAX];
    size_t pubkey_len = 0, seckey_len = 0;

    bool ok = crypto_keygen(ctx, pubkey, &pubkey_len, seckey, &seckey_len);
    assert(ok && "crypto_keygen failed");
    printf("[OK] keygen: pubkey %zu bytes, seckey %zu bytes\n", pubkey_len, seckey_len);

    const char *msg = "hello qMEMO blockchain";
    size_t msg_len = strlen(msg);

    uint8_t sig[CRYPTO_SIG_MAX];
    size_t sig_len = 0;

    ok = crypto_sign(ctx, sig, &sig_len, (const uint8_t *)msg, msg_len, seckey, seckey_len);
    assert(ok && "crypto_sign failed");
    printf("[OK] sign:   sig %zu bytes\n", sig_len);

    ok = crypto_verify(ctx, sig, sig_len, (const uint8_t *)msg, msg_len, pubkey, pubkey_len);
    assert(ok && "crypto_verify should pass on valid sig");
    printf("[OK] verify: valid signature accepted\n");

    /* Tamper with signature */
    sig[0] ^= 0xFF;
    ok = crypto_verify(ctx, sig, sig_len, (const uint8_t *)msg, msg_len, pubkey, pubkey_len);
    assert(!ok && "crypto_verify should reject tampered signature");
    sig[0] ^= 0xFF;  /* restore */
    printf("[OK] verify: tampered signature rejected\n");

    /* Tamper with message */
    const char *bad_msg = "hello qMEMO blockchaiN";
    ok = crypto_verify(ctx, sig, sig_len, (const uint8_t *)bad_msg, strlen(bad_msg),
                       pubkey, pubkey_len);
    assert(!ok && "crypto_verify should reject wrong message");
    printf("[OK] verify: wrong message rejected\n");

    /* Test crypto_verify_typed() — stateless path used by transaction_verify */
    ok = crypto_verify_typed(sig_type, sig, sig_len, (const uint8_t *)msg, msg_len,
                             pubkey, pubkey_len);
    assert(ok && "crypto_verify_typed should pass");
    printf("[OK] crypto_verify_typed: passed\n");

    printf("  Pubkey:     %zu / %d bytes (actual / max)\n", pubkey_len, CRYPTO_PUBKEY_MAX);
    printf("  Signature:  %zu / %d bytes (actual / max)\n\n", sig_len, CRYPTO_SIG_MAX);

    crypto_ctx_free(ctx);
    return 0;
}

int main(void) {
    printf("=== crypto_backend round-trip test ===\n");
    printf("Scheme: %s  (SIG_SCHEME=%d)\n\n", CRYPTO_SCHEME_NAME, SIG_SCHEME);

    int failures = 0;

#if SIG_SCHEME == SIG_HYBRID
    /* Hybrid mode: test both backends independently */
    failures += test_scheme(SIG_ECDSA, "ECDSA");
    failures += test_scheme(SIG_FALCON512, "Falcon-512");

    /* Cross-scheme rejection: sign with ECDSA, verify as Falcon → must fail */
    printf("--- Cross-scheme rejection ---\n");
    {
        crypto_ctx_t *ctx_e = crypto_ctx_new(SIG_ECDSA);
        assert(ctx_e);
        uint8_t pk[CRYPTO_PUBKEY_MAX], sk[CRYPTO_SECKEY_MAX];
        size_t pk_len = 0, sk_len = 0;
        assert(crypto_keygen(ctx_e, pk, &pk_len, sk, &sk_len));

        const char *msg = "cross-scheme test";
        uint8_t sig[CRYPTO_SIG_MAX];
        size_t sig_len = 0;
        assert(crypto_sign(ctx_e, sig, &sig_len, (const uint8_t *)msg, strlen(msg), sk, sk_len));

        /* Verify with wrong scheme type */
        bool ok = crypto_verify_typed(SIG_FALCON512, sig, sig_len,
                                      (const uint8_t *)msg, strlen(msg), pk, pk_len);
        assert(!ok && "cross-scheme verify should fail");
        printf("[OK] ECDSA sig rejected when verified as Falcon-512\n");

        /* Same sig verified with correct scheme */
        ok = crypto_verify_typed(SIG_ECDSA, sig, sig_len,
                                 (const uint8_t *)msg, strlen(msg), pk, pk_len);
        assert(ok && "same-scheme verify should pass");
        printf("[OK] ECDSA sig accepted when verified as ECDSA\n\n");

        crypto_ctx_free(ctx_e);
    }
#else
    /* Single-scheme mode */
    uint8_t scheme;
    if (SIG_SCHEME == SIG_FALCON512) scheme = SIG_FALCON512;
    else if (SIG_SCHEME == SIG_ML_DSA44) scheme = SIG_ML_DSA44;
    else scheme = SIG_ECDSA;
    failures += test_scheme(scheme, CRYPTO_SCHEME_NAME);
#endif

    if (failures == 0) {
        printf("=== ALL TESTS PASSED ===\n");
    } else {
        printf("=== %d TEST(S) FAILED ===\n", failures);
    }

    return failures;
}
