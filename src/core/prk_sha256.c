/**
 * @file prk_sha256.c
 * @brief SHA-256 (FIPS 180-4) for verifying downloaded installers (#75). Portable C, no IO.
 */
#include "proofrunkit/prk_sha256.h"

#include <string.h>

/** @brief Round constants: the first 32 bits of the fractional parts of the cube roots of the first 64 primes. */
static const uint32_t prk_sha256_rounds[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

static uint32_t rotate_right(uint32_t value, unsigned count) {
    return (value >> count) | (value << (32u - count));
}

/** @brief Processes one 64-byte block into @p state. */
static void compress_block(uint32_t state[8], const unsigned char block[64]) {
    uint32_t words[64];
    for (unsigned index = 0; index < 16; ++index) {
        words[index] = ((uint32_t)block[4 * index] << 24) | ((uint32_t)block[4 * index + 1] << 16) |
                       ((uint32_t)block[4 * index + 2] << 8) | (uint32_t)block[4 * index + 3];
    }
    for (unsigned index = 16; index < 64; ++index) {
        const uint32_t s0 =
            rotate_right(words[index - 15], 7) ^ rotate_right(words[index - 15], 18) ^ (words[index - 15] >> 3);
        const uint32_t s1 =
            rotate_right(words[index - 2], 17) ^ rotate_right(words[index - 2], 19) ^ (words[index - 2] >> 10);
        words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }
    uint32_t work[8];
    memcpy(work, state, sizeof work);
    for (unsigned index = 0; index < 64; ++index) {
        const uint32_t s1 = rotate_right(work[4], 6) ^ rotate_right(work[4], 11) ^ rotate_right(work[4], 25);
        const uint32_t choice = (work[4] & work[5]) ^ (~work[4] & work[6]);
        const uint32_t first = work[7] + s1 + choice + prk_sha256_rounds[index] + words[index];
        const uint32_t s0 = rotate_right(work[0], 2) ^ rotate_right(work[0], 13) ^ rotate_right(work[0], 22);
        const uint32_t majority = (work[0] & work[1]) ^ (work[0] & work[2]) ^ (work[1] & work[2]);
        memmove(work + 1, work, 7 * sizeof work[0]);
        work[4] += first;
        work[0] = first + s0 + majority;
    }
    for (unsigned index = 0; index < 8; ++index) {
        state[index] += work[index];
    }
}

void prk_start_sha256(PrkSha256* out_hash) {
    static const uint32_t initial[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    memset(out_hash, 0, sizeof *out_hash);
    memcpy(out_hash->state, initial, sizeof initial);
}

void prk_update_sha256(PrkSha256* hash, const void* bytes, size_t length) {
    const unsigned char* cursor = (const unsigned char*)bytes;
    hash->total_bytes += length;
    while (length > 0) {
        const size_t room = sizeof hash->block - hash->block_length;
        const size_t taken = length < room ? length : room;
        memcpy(hash->block + hash->block_length, cursor, taken);
        hash->block_length += taken;
        cursor += taken;
        length -= taken;
        if (hash->block_length == sizeof hash->block) {
            compress_block(hash->state, hash->block);
            hash->block_length = 0;
        }
    }
}

void prk_finish_sha256(PrkSha256* hash, unsigned char out_digest[PRK_SHA256_DIGEST_SIZE]) {
    const uint64_t total_bits = hash->total_bytes * 8u;
    static const unsigned char padding[64] = {0x80};
    const size_t pad_length = hash->block_length < 56 ? 56 - hash->block_length : 120 - hash->block_length;
    prk_update_sha256(hash, padding, pad_length);
    unsigned char length_bytes[8];
    for (unsigned index = 0; index < 8; ++index) {
        length_bytes[index] = (unsigned char)(total_bits >> (56u - 8u * index));
    }
    prk_update_sha256(hash, length_bytes, sizeof length_bytes);
    for (unsigned index = 0; index < 8; ++index) {
        out_digest[4 * index] = (unsigned char)(hash->state[index] >> 24);
        out_digest[4 * index + 1] = (unsigned char)(hash->state[index] >> 16);
        out_digest[4 * index + 2] = (unsigned char)(hash->state[index] >> 8);
        out_digest[4 * index + 3] = (unsigned char)hash->state[index];
    }
}

void prk_format_sha256_hex(const void* bytes, size_t length, char out_hex[PRK_SHA256_HEX_LENGTH + 1]) {
    static const char digits[] = "0123456789abcdef";
    PrkSha256 hash;
    unsigned char digest[PRK_SHA256_DIGEST_SIZE];
    prk_start_sha256(&hash);
    prk_update_sha256(&hash, bytes, length);
    prk_finish_sha256(&hash, digest);
    for (unsigned index = 0; index < PRK_SHA256_DIGEST_SIZE; ++index) {
        out_hex[2 * index] = digits[digest[index] >> 4];
        out_hex[2 * index + 1] = digits[digest[index] & 0x0Fu];
    }
    out_hex[PRK_SHA256_HEX_LENGTH] = '\0';
}
