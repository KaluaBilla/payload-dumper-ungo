/*
 * SHA-256 Header-Only Implementation
 * Based on FIPS 180-4 specification
 * Public Domain / MIT License
 * 
 * Usage:
 *   SHA256_CTX ctx;
 *   sha256_init(&ctx);
 *   sha256_update(&ctx, data, len);
 *   sha256_final(&ctx, hash);
 */

#ifndef SHA256_H
#define SHA256_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHA256_BLOCK_SIZE 64
#define SHA256_DIGEST_SIZE 32

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[SHA256_BLOCK_SIZE];
} SHA256_CTX;

/* Right rotate macro */
#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

/* SHA-256 functions */
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x)       (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x)       (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x)      (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x)      (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

/* SHA-256 constants (first 32 bits of fractional parts of cube roots of first 64 primes) */
static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/* Transform function - processes one 512-bit block */
static inline void sha256_transform(SHA256_CTX *ctx, const uint8_t *data)
{
    uint32_t a, b, c, d, e, f, g, h, t1, t2, m[64];
    int i;

    /* Prepare message schedule (expand 16 words to 64) */
    for (i = 0; i < 16; i++) {
        m[i] = ((uint32_t)data[i * 4 + 0] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               ((uint32_t)data[i * 4 + 3]);
    }
    for (i = 16; i < 64; i++) {
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];
    }

    /* Initialize working variables */
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    /* 64 rounds */
    for (i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e, f, g) + K[i] + m[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    /* Update state */
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

/* Initialize SHA-256 context */
static inline void sha256_init(SHA256_CTX *ctx)
{
    /* Initial hash values (first 32 bits of fractional parts of square roots of first 8 primes) */
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

/* Update SHA-256 with new data */
static inline void sha256_update(SHA256_CTX *ctx, const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t buffer_space = SHA256_BLOCK_SIZE - (ctx->count % SHA256_BLOCK_SIZE);

    ctx->count += len;

    /* If we have leftover data in buffer and new data fills it */
    if (len >= buffer_space) {
        memcpy(ctx->buffer + (SHA256_BLOCK_SIZE - buffer_space), bytes, buffer_space);
        sha256_transform(ctx, ctx->buffer);
        bytes += buffer_space;
        len -= buffer_space;

        /* Process complete blocks */
        while (len >= SHA256_BLOCK_SIZE) {
            sha256_transform(ctx, bytes);
            bytes += SHA256_BLOCK_SIZE;
            len -= SHA256_BLOCK_SIZE;
        }

        buffer_space = SHA256_BLOCK_SIZE;
    }

    /* Store remaining data in buffer */
    if (len > 0) {
        memcpy(ctx->buffer + (SHA256_BLOCK_SIZE - buffer_space), bytes, len);
    }
}

/* Finalize SHA-256 and output hash */
static inline void sha256_final(SHA256_CTX *ctx, uint8_t hash[SHA256_DIGEST_SIZE])
{
    size_t i = ctx->count % SHA256_BLOCK_SIZE;
    uint8_t end = SHA256_BLOCK_SIZE - i;

    /* Padding: append 0x80, then zeros */
    ctx->buffer[i++] = 0x80;

    /* If not enough space for length, pad this block and add another */
    if (end < 9) {
        memset(ctx->buffer + i, 0, end - 1);
        sha256_transform(ctx, ctx->buffer);
        i = 0;
        end = SHA256_BLOCK_SIZE;
    }

    /* Pad with zeros until 8 bytes before end */
    memset(ctx->buffer + i, 0, end - 9);

    /* Append length in bits (big-endian) */
    uint64_t bit_count = ctx->count * 8;
    ctx->buffer[63] = (uint8_t)(bit_count);
    ctx->buffer[62] = (uint8_t)(bit_count >> 8);
    ctx->buffer[61] = (uint8_t)(bit_count >> 16);
    ctx->buffer[60] = (uint8_t)(bit_count >> 24);
    ctx->buffer[59] = (uint8_t)(bit_count >> 32);
    ctx->buffer[58] = (uint8_t)(bit_count >> 40);
    ctx->buffer[57] = (uint8_t)(bit_count >> 48);
    ctx->buffer[56] = (uint8_t)(bit_count >> 56);

    sha256_transform(ctx, ctx->buffer);

    /* Output hash (big-endian) */
    for (i = 0; i < 8; i++) {
        hash[i * 4 + 0] = (ctx->state[i] >> 24) & 0xff;
        hash[i * 4 + 1] = (ctx->state[i] >> 16) & 0xff;
        hash[i * 4 + 2] = (ctx->state[i] >> 8) & 0xff;
        hash[i * 4 + 3] = (ctx->state[i]) & 0xff;
    }
}

/* Convenience function -> hash data in one call */
static inline void sha256(const void *data, size_t len, uint8_t hash[SHA256_DIGEST_SIZE])
{
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, hash);
}

/* convert hash to hex string */
static inline void sha256_to_hex(const uint8_t hash[SHA256_DIGEST_SIZE], char hex[65])
{
    static const char hex_chars[] = "0123456789abcdef";
    int i;
    for (i = 0; i < SHA256_DIGEST_SIZE; i++) {
        hex[i * 2 + 0] = hex_chars[(hash[i] >> 4) & 0xf];
        hex[i * 2 + 1] = hex_chars[hash[i] & 0xf];
    }
    hex[64] = '\0';
}

#ifdef __cplusplus
}
#endif

#endif /* SHA256_H */
