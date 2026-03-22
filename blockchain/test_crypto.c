/**
 * test_crypto.c - Round-trip sign/verify test for crypto_backend
 *
 * Build & run:
 *   make test_crypto                  # ECDSA (default)
 *   make test_crypto SIG_SCHEME=2     # Falcon-512
 */
#include "crypto_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

int main(void) {
    printf("=== crypto_backend round-trip test ===\n");
    printf("Scheme: %s  (SIG_SCHEME=%d)\n\n", CRYPTO_SCHEME_NAME, SIG_SCHEME);

    /* 1. Create context and generate keypair */
    crypto_ctx_t *ctx = crypto_ctx_new();
    assert(ctx && "crypto_ctx_new failed");

    uint8_t pubkey[CRYPTO_PUBKEY_MAX];
    uint8_t seckey[CRYPTO_SECKEY_MAX];
    size_t pubkey_len = 0, seckey_len = 0;

    bool ok = crypto_keygen(ctx, pubkey, &pubkey_len, seckey, &seckey_len);
    assert(ok && "crypto_keygen failed");
    printf("[OK] keygen: pubkey %zu bytes, seckey %zu bytes\n", pubkey_len, seckey_len);

    /* 2. Sign a message */
    const char *msg = "hello qMEMO blockchain";
    size_t msg_len = strlen(msg);

    uint8_t sig[CRYPTO_SIG_MAX];
    size_t sig_len = 0;

    ok = crypto_sign(ctx, sig, &sig_len, (const uint8_t *)msg, msg_len, seckey, seckey_len);
    assert(ok && "crypto_sign failed");
    printf("[OK] sign:   sig %zu bytes\n", sig_len);

    /* 3. Verify — should pass */
    ok = crypto_verify(ctx, sig, sig_len, (const uint8_t *)msg, msg_len, pubkey, pubkey_len);
    assert(ok && "crypto_verify should pass on valid sig");
    printf("[OK] verify: valid signature accepted\n");

    /* 4. Tamper with signature — should fail */
    sig[0] ^= 0xFF;
    ok = crypto_verify(ctx, sig, sig_len, (const uint8_t *)msg, msg_len, pubkey, pubkey_len);
    assert(!ok && "crypto_verify should reject tampered signature");
    sig[0] ^= 0xFF;  /* restore */
    printf("[OK] verify: tampered signature rejected\n");

    /* 5. Tamper with message — should fail */
    const char *bad_msg = "hello qMEMO blockchaiN";  /* last char changed */
    ok = crypto_verify(ctx, sig, sig_len, (const uint8_t *)bad_msg, strlen(bad_msg),
                       pubkey, pubkey_len);
    assert(!ok && "crypto_verify should reject wrong message");
    printf("[OK] verify: wrong message rejected\n");

    /* Summary */
    printf("\n=== ALL TESTS PASSED ===\n");
    printf("  Scheme:     %s\n", CRYPTO_SCHEME_NAME);
    printf("  Pubkey:     %zu / %d bytes (actual / max)\n", pubkey_len, CRYPTO_PUBKEY_MAX);
    printf("  Signature:  %zu / %d bytes (actual / max)\n", sig_len, CRYPTO_SIG_MAX);

    crypto_ctx_free(ctx);
    return 0;
}
