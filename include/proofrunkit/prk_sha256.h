/**
 * @file prk_sha256.h
 * @brief SHA-256 (FIPS 180-4) for verifying downloaded installers (#75). Portable C, no IO.
 */
#ifndef PROOFRUNKIT_PRK_SHA256_H
#define PROOFRUNKIT_PRK_SHA256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Bytes in a SHA-256 digest. */
#define PRK_SHA256_DIGEST_SIZE 32
/** @brief Characters in a lower-case hex digest, without the terminator. */
#define PRK_SHA256_HEX_LENGTH 64

/** @brief Incremental SHA-256 state. Initialise with prk_start_sha256(). */
typedef struct PrkSha256 {
    uint32_t state[8];       /**< Hash state H0..H7. */
    uint64_t total_bytes;    /**< Bytes consumed so far. */
    unsigned char block[64]; /**< Pending partial block. */
    size_t block_length;     /**< Bytes in block. */
} PrkSha256;

/**
 * @brief Initialises @p out_hash.
 * @param out_hash State to initialise. @return Nothing. Pure function.
 */
void prk_start_sha256(PrkSha256* out_hash);

/**
 * @brief Feeds @p length bytes into @p hash (a documented mutator).
 * @param hash State from prk_start_sha256(). @param bytes Data; may be NULL when @p length is 0.
 * @param length Bytes in @p bytes. @return Nothing. Pure function.
 */
void prk_update_sha256(PrkSha256* hash, const void* bytes, size_t length);

/**
 * @brief Finishes @p hash and writes the digest; @p hash must be restarted before reuse.
 * @param hash State (consumed). @param out_digest Receives 32 bytes. @return Nothing. Pure function.
 */
void prk_finish_sha256(PrkSha256* hash, unsigned char out_digest[PRK_SHA256_DIGEST_SIZE]);

/**
 * @brief Hashes @p bytes in one call and writes the lower-case hex digest.
 * @param bytes Data; may be NULL when @p length is 0. @param length Bytes in @p bytes.
 * @param out_hex Receives 64 hex characters and a terminator (65 bytes). @return Nothing. Pure function.
 */
void prk_format_sha256_hex(const void* bytes, size_t length, char out_hex[PRK_SHA256_HEX_LENGTH + 1]);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PROOFRUNKIT_PRK_SHA256_H */
