#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The parts of a WebAuthn reply that need no CBOR: the browser hands the public key over as SPKI. */

#define DB_AD_UP 0x01 /* user present */
#define DB_AD_UV 0x04 /* user verified */
#define DB_AD_AT 0x40 /* a new credential follows */

typedef struct {
    const uint8_t *rp_hash; /* SHA-256 of the RP ID, 32 bytes */
    uint8_t flags;
    uint32_t count;
    const uint8_t *cred; /* the new credential's id, with DB_AD_AT only */
    size_t cred_len;
} db_authdata_t;

bool db_authdata(const uint8_t *ad, size_t len, db_authdata_t *out);

/* The point x||y of a P-256 public key in SPKI DER. */
bool db_spki_p256(const uint8_t *der, size_t len, uint8_t xy[64]);

/* A login's signature counter after the stored one: higher, or 0 from an authenticator that never counts. */
bool db_count_ok(uint32_t stored, uint32_t got);

/* Base64url with or without padding. The length, or -1 for bad input or too little room. */
int db_b64url(const char *in, uint8_t *out, size_t cap);
