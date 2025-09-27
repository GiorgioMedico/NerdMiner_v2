#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "test_utils.h"
#include "fixtures/sha256_test_vectors.h"
#include "fixtures/mining_test_vectors.h"

// Only compile this for native tests
#ifdef NATIVE_TEST

// Mock Arduino functions for native testing
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

// Real SHA256 implementation
// SHA256 constants
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

// Right rotate function
#define ROTR(n, x) (((x) >> (n)) | ((x) << (32 - (n))))

// SHA256 logical functions
#define CH(x, y, z)    (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z)   (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SIGMA0(x)      (ROTR(2, x) ^ ROTR(13, x) ^ ROTR(22, x))
#define SIGMA1(x)      (ROTR(6, x) ^ ROTR(11, x) ^ ROTR(25, x))
#define sigma0(x)      (ROTR(7, x) ^ ROTR(18, x) ^ ((x) >> 3))
#define sigma1(x)      (ROTR(17, x) ^ ROTR(19, x) ^ ((x) >> 10))

// Real SHA256 implementation
void sha256_real(const uint8_t* input, size_t input_len, uint8_t* output) {
    // Initial hash values
    uint32_t H[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    // Pre-processing: adding padding bits and length
    size_t padded_len = input_len + 1; // +1 for the '1' bit
    while (padded_len % 64 != 56) {
        padded_len++;
    }
    padded_len += 8; // +8 for the 64-bit length

    uint8_t* padded = (uint8_t*)malloc(padded_len);
    memcpy(padded, input, input_len);

    // Add padding
    padded[input_len] = 0x80; // append '1' bit
    memset(padded + input_len + 1, 0, padded_len - input_len - 9);

    // Add length in bits as big-endian 64-bit integer
    uint64_t bit_len = input_len * 8;
    for (int i = 0; i < 8; i++) {
        padded[padded_len - 8 + i] = (bit_len >> (56 - 8 * i)) & 0xff;
    }

    // Process message in 512-bit chunks
    for (size_t chunk = 0; chunk < padded_len; chunk += 64) {
        uint32_t W[64];

        // Copy chunk into first 16 words W[0..15] of the message schedule array
        for (int i = 0; i < 16; i++) {
            W[i] = (padded[chunk + i * 4] << 24) |
                   (padded[chunk + i * 4 + 1] << 16) |
                   (padded[chunk + i * 4 + 2] << 8) |
                   (padded[chunk + i * 4 + 3]);
        }

        // Extend the first 16 words into the remaining 48 words W[16..63]
        for (int i = 16; i < 64; i++) {
            W[i] = sigma1(W[i - 2]) + W[i - 7] + sigma0(W[i - 15]) + W[i - 16];
        }

        // Initialize working variables
        uint32_t a = H[0], b = H[1], c = H[2], d = H[3];
        uint32_t e = H[4], f = H[5], g = H[6], h = H[7];

        // Compression function main loop
        for (int i = 0; i < 64; i++) {
            uint32_t T1 = h + SIGMA1(e) + CH(e, f, g) + K[i] + W[i];
            uint32_t T2 = SIGMA0(a) + MAJ(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + T1;
            d = c;
            c = b;
            b = a;
            a = T1 + T2;
        }

        // Add the compressed chunk to the current hash value
        H[0] += a; H[1] += b; H[2] += c; H[3] += d;
        H[4] += e; H[5] += f; H[6] += g; H[7] += h;
    }

    // Produce the final hash value as a 256-bit number (big-endian)
    for (int i = 0; i < 8; i++) {
        output[i * 4] = (H[i] >> 24) & 0xff;
        output[i * 4 + 1] = (H[i] >> 16) & 0xff;
        output[i * 4 + 2] = (H[i] >> 8) & 0xff;
        output[i * 4 + 3] = H[i] & 0xff;
    }

    free(padded);
}

// Real SHA256 implementation wrapper
void reference_sha256(const uint8_t* input, size_t input_len, uint8_t* output) {
    sha256_real(input, input_len, output);
}

void reference_sha256_double(const uint8_t* input, size_t input_len, uint8_t* output) {
    uint8_t first_hash[32];
    reference_sha256(input, input_len, first_hash);
    reference_sha256(first_hash, 32, output);
}

// Validation functions needed by tests
bool validate_mining_job(const struct test_mining_job* job) {
    if (!job) return false;
    if (!job->job_id || strlen(job->job_id) == 0) return false;
    if (!job->prev_block_hash || strlen(job->prev_block_hash) != 64) return false;
    if (!job->coinb1 || !job->coinb2) return false;
    if (!job->nbits || strlen(job->nbits) != 8) return false;
    if (!job->version || strlen(job->version) != 8) return false;
    if (!job->ntime || strlen(job->ntime) != 8) return false;
    if (job->merkle_branch_count < 0 || job->merkle_branch_count > 8) return false;

    // Validate merkle branches
    for (int i = 0; i < job->merkle_branch_count; i++) {
        if (!job->merkle_branches[i] || strlen(job->merkle_branches[i]) != 64) {
            return false;
        }
    }

    return true;
}

bool validate_block_header(const uint8_t* header) {
    if (!header) return false;

    // Basic validation - check that it's not all zeros
    bool all_zeros = true;
    for (int i = 0; i < 80; i++) {
        if (header[i] != 0) {
            all_zeros = false;
            break;
        }
    }

    return !all_zeros;
}

bool validate_nonce_in_range(uint32_t nonce, uint32_t min_nonce, uint32_t max_nonce) {
    return (nonce >= min_nonce && nonce <= max_nonce);
}

bool validate_difficulty_calculation(double difficulty, const uint8_t* target) {
    if (!target) return false;
    if (difficulty <= 0.0) return false;

    // Very basic validation - check that target is reasonable
    // First 4 bytes should be small for reasonable difficulties
    if (target[0] != 0x00 || target[1] != 0x00) return false;

    return true;
}

// Test functions
void setUp(void) {
    // Common setup for each test
}

void tearDown(void) {
    // Common cleanup for each test
}

//=============================================================================
// SHA256 TESTS
//=============================================================================

// Test basic SHA256 functionality with known test vectors
void test_sha256_empty_string(void) {
    uint8_t expected[32];
    uint8_t actual[32];

    hex_string_to_bytes(SHA256_TV1_EXPECTED, expected, 32);
    reference_sha256((const uint8_t*)"", 0, actual);

    assert_bytes_equal(expected, actual, 32, "SHA256 empty string test failed");
}

void test_sha256_abc(void) {
    uint8_t expected[32];
    uint8_t actual[32];

    hex_string_to_bytes(SHA256_TV2_EXPECTED, expected, 32);
    reference_sha256((const uint8_t*)"abc", 3, actual);

    assert_bytes_equal(expected, actual, 32, "SHA256 'abc' test failed");
}

void test_sha256_message_digest(void) {
    uint8_t expected[32];
    uint8_t actual[32];

    hex_string_to_bytes(SHA256_TV3_EXPECTED, expected, 32);
    reference_sha256((const uint8_t*)"message digest", 14, actual);

    assert_bytes_equal(expected, actual, 32, "SHA256 'message digest' test failed");
}

// Test double SHA256 (Bitcoin-style)
void test_sha256_double_hello(void) {
    uint8_t expected[32];
    uint8_t actual[32];

    hex_string_to_bytes(SHA256_DOUBLE_TV1_EXPECTED, expected, 32);
    reference_sha256_double((const uint8_t*)"hello", 5, actual);

    assert_bytes_equal(expected, actual, 32, "SHA256 double 'hello' test failed");
}

// Test endian conversion utilities
void test_endian_conversions(void) {
    uint32_t value = 0x12345678;
    uint32_t swapped = __builtin_bswap32(value);

    TEST_ASSERT_EQUAL_HEX32(0x78563412, swapped);
}

// Test byte array utilities
void test_hex_string_conversion(void) {
    const char* hex_input = "deadbeef";
    uint8_t bytes[4];
    char hex_output[9];

    hex_string_to_bytes(hex_input, bytes, 4);
    bytes_to_hex_string(bytes, 4, hex_output);

    TEST_ASSERT_EQUAL_STRING(hex_input, hex_output);
    TEST_ASSERT_EQUAL_HEX8(0xde, bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0xad, bytes[1]);
    TEST_ASSERT_EQUAL_HEX8(0xbe, bytes[2]);
    TEST_ASSERT_EQUAL_HEX8(0xef, bytes[3]);
}

// Test SHA256 hash validation
void test_sha256_hash_validation(void) {
    uint8_t valid_hash[32];
    uint8_t invalid_hash[32];

    hex_string_to_bytes(SHA256_TV2_EXPECTED, valid_hash, 32);
    memset(invalid_hash, 0, 32); // All zeros is invalid

    TEST_ASSERT_TRUE(validate_sha256_hash(valid_hash));
    TEST_ASSERT_FALSE(validate_sha256_hash(invalid_hash));
    TEST_ASSERT_FALSE(validate_sha256_hash(NULL));
}

// Test Bitcoin block header structure
void test_bitcoin_block_header_structure(void) {
    // Test block header size
    TEST_ASSERT_EQUAL(80, BITCOIN_BLOCK_HEADER_SIZE);

    // Test that our test vector has the correct size
    TEST_ASSERT_EQUAL(80, sizeof(BITCOIN_BLOCK_HEADER_TV1));

    // Test version field (first 4 bytes, little-endian)
    uint32_t version = *((uint32_t*)&BITCOIN_BLOCK_HEADER_TV1[0]);
    TEST_ASSERT_EQUAL(1, version); // Version 1 in little-endian

    // Test timestamp field (bytes 68-71)
    uint32_t timestamp = *((uint32_t*)&BITCOIN_BLOCK_HEADER_TV1[68]);
    TEST_ASSERT_TRUE(timestamp > 0);

    // Test nonce field (bytes 76-79)
    uint32_t nonce = *((uint32_t*)&BITCOIN_BLOCK_HEADER_TV1[76]);
    TEST_ASSERT_TRUE(nonce > 0);
}

//=============================================================================
// MINING TESTS
//=============================================================================

// Test mining data structure initialization
void test_mining_data_structure(void) {
    // Test miner_data structure size and alignment
    struct mock_miner_data {
        uint8_t bytearray_target[32];
        uint8_t bytearray_pooltarget[32];
        uint8_t merkle_result[32];
        uint8_t bytearray_blockheader[128];
    };

    mock_miner_data data;
    memset(&data, 0, sizeof(data));

    // Test structure size
    TEST_ASSERT_EQUAL(224, sizeof(data)); // 32+32+32+128 = 224 bytes

    // Test zero initialization
    for (int i = 0; i < 32; i++) {
        TEST_ASSERT_EQUAL(0, data.bytearray_target[i]);
        TEST_ASSERT_EQUAL(0, data.bytearray_pooltarget[i]);
        TEST_ASSERT_EQUAL(0, data.merkle_result[i]);
    }

    for (int i = 0; i < 128; i++) {
        TEST_ASSERT_EQUAL(0, data.bytearray_blockheader[i]);
    }
}

// Test difficulty target validation
void test_difficulty_target_validation(void) {
    uint8_t valid_target[32];
    uint8_t invalid_target[32];

    // Copy test vectors
    memcpy(valid_target, DIFFICULTY_TARGET_EASY, 32);
    memset(invalid_target, 0xFF, 32); // Invalid: too high

    TEST_ASSERT_TRUE(validate_bitcoin_difficulty_target(valid_target));
    TEST_ASSERT_FALSE(validate_bitcoin_difficulty_target(invalid_target));
}

// Test nonce range validation
void test_nonce_range_validation(void) {
    // Test nonce constants (if defined)
#ifdef MAX_NONCE_STEP
    TEST_ASSERT_TRUE(MAX_NONCE_STEP > 0);
#endif
#ifdef MAX_NONCE
    TEST_ASSERT_TRUE(MAX_NONCE > 1000000); // Should be reasonably large
#endif

    // Test nonce range logic
    uint32_t nonce_start = 0;
    uint32_t nonce_end = 5000000;

    TEST_ASSERT_TRUE(nonce_end > nonce_start);
    TEST_ASSERT_TRUE((nonce_end - nonce_start) <= 25000000);
}

// Test mining job validation
void test_mining_job_validation(void) {
    // Test valid mining jobs
    TEST_ASSERT_TRUE(validate_mining_job(&TEST_MINING_JOB_1));
    TEST_ASSERT_TRUE(validate_mining_job(&TEST_MINING_JOB_2));

    // Test invalid mining job (NULL)
    TEST_ASSERT_FALSE(validate_mining_job(NULL));
}

// Test block header validation
void test_block_header_validation(void) {
    // Test valid block header
    TEST_ASSERT_TRUE(validate_block_header(TEST_BLOCK_HEADER_TEMPLATE));

    // Test invalid block header (NULL)
    TEST_ASSERT_FALSE(validate_block_header(NULL));

    // Test invalid block header (all zeros)
    uint8_t zero_header[80];
    memset(zero_header, 0, 80);
    TEST_ASSERT_FALSE(validate_block_header(zero_header));
}

// Test difficulty calculation validation
void test_difficulty_calculation(void) {
    // Test valid difficulty
    TEST_ASSERT_TRUE(validate_difficulty_calculation(TEST_DIFFICULTY_EASY, DIFFICULTY_TARGET_EASY));
    TEST_ASSERT_TRUE(validate_difficulty_calculation(TEST_DIFFICULTY_MEDIUM, DIFFICULTY_TARGET_MEDIUM));
    TEST_ASSERT_TRUE(validate_difficulty_calculation(TEST_DIFFICULTY_HARD, DIFFICULTY_TARGET_HARD));

    // Test invalid difficulty (zero or negative)
    TEST_ASSERT_FALSE(validate_difficulty_calculation(0.0, DIFFICULTY_TARGET_EASY));
    TEST_ASSERT_FALSE(validate_difficulty_calculation(-1.0, DIFFICULTY_TARGET_EASY));

    // Test invalid target (NULL)
    TEST_ASSERT_FALSE(validate_difficulty_calculation(TEST_DIFFICULTY_EASY, NULL));
}

// Test nonce validation
void test_nonce_validation(void) {
    uint32_t min_nonce = 0;
    uint32_t max_nonce = 25000000;

    // Test valid nonces
    TEST_ASSERT_TRUE(validate_nonce_in_range(0, min_nonce, max_nonce));
    TEST_ASSERT_TRUE(validate_nonce_in_range(12500000, min_nonce, max_nonce));
    TEST_ASSERT_TRUE(validate_nonce_in_range(25000000, min_nonce, max_nonce));

    // Test invalid nonces
    TEST_ASSERT_FALSE(validate_nonce_in_range(25000001, min_nonce, max_nonce));
    TEST_ASSERT_FALSE(validate_nonce_in_range(UINT32_MAX, min_nonce, max_nonce));
}

// Test mining constants
void test_mining_constants(void) {
    // Test that constants are defined and reasonable
#ifdef MAX_NONCE_STEP
    TEST_ASSERT_TRUE(MAX_NONCE_STEP >= 1000000);   // At least 1M
    TEST_ASSERT_TRUE(MAX_NONCE_STEP <= 10000000);  // At most 10M
#endif

#ifdef MAX_NONCE
    TEST_ASSERT_TRUE(MAX_NONCE >= 10000000);       // At least 10M
    TEST_ASSERT_TRUE(MAX_NONCE <= 100000000);      // At most 100M
#endif

#ifdef DEFAULT_DIFFICULTY
    TEST_ASSERT_TRUE(DEFAULT_DIFFICULTY > 0.0);
    TEST_ASSERT_TRUE(DEFAULT_DIFFICULTY < 1.0);
#endif
}

int main(int argc, char **argv) {
    UNITY_BEGIN();

    // SHA256 Tests
    RUN_TEST(test_hex_string_conversion);
    RUN_TEST(test_endian_conversions);
    RUN_TEST(test_sha256_empty_string);
    RUN_TEST(test_sha256_abc);
    RUN_TEST(test_sha256_message_digest);
    RUN_TEST(test_sha256_double_hello);
    RUN_TEST(test_sha256_hash_validation);
    RUN_TEST(test_bitcoin_block_header_structure);

    // Mining Tests
    RUN_TEST(test_mining_data_structure);
    RUN_TEST(test_difficulty_target_validation);
    RUN_TEST(test_nonce_range_validation);
    RUN_TEST(test_mining_job_validation);
    RUN_TEST(test_block_header_validation);
    RUN_TEST(test_difficulty_calculation);
    RUN_TEST(test_nonce_validation);
    RUN_TEST(test_mining_constants);

    return UNITY_END();
}

#endif // NATIVE_TEST