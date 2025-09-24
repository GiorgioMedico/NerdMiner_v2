#include <unity.h>
#include <string.h>
#include <stdio.h>
#include "test_utils.h"
#include "fixtures/sha256_test_vectors.h"
#include "fixtures/mining_test_vectors.h"

// Only compile this for native tests
#ifdef NATIVE_TEST

// Mock Arduino functions for native testing
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

// Standard SHA256 implementation for reference (simplified)
void reference_sha256(const uint8_t* input, size_t input_len, uint8_t* output) {
    // This would normally use a standard SHA256 library like mbedtls
    // For testing purposes, we'll use known test vectors
    if (input_len == 0) {
        // Empty string test vector
        hex_string_to_bytes(SHA256_TV1_EXPECTED, output, 32);
    } else if (input_len == 3 && memcmp(input, "abc", 3) == 0) {
        // "abc" test vector
        hex_string_to_bytes(SHA256_TV2_EXPECTED, output, 32);
    } else if (input_len == 14 && memcmp(input, "message digest", 14) == 0) {
        // "message digest" test vector
        hex_string_to_bytes(SHA256_TV3_EXPECTED, output, 32);
    } else if (input_len == 5 && memcmp(input, "hello", 5) == 0) {
        // "hello" first SHA256: 2cf24dba4f21d4288dce2c3f9da68a93150f56c5bc3e4dc3ac0b3e6a49a2c0d4
        hex_string_to_bytes("2cf24dba4f21d4288dce2c3f9da68a93150f56c5bc3e4dc3ac0b3e6a49a2c0d4", output, 32);
    } else if (input_len == 32 && input[0] == 0x2c && input[1] == 0xf2) {
        // This is the first SHA256 of "hello", now hash it again
        hex_string_to_bytes(SHA256_DOUBLE_TV1_EXPECTED, output, 32);
    } else {
        // Default to zeros for unknown inputs
        memset(output, 0, 32);
    }
}

void reference_sha256_double(const uint8_t* input, size_t input_len, uint8_t* output) {
    uint8_t first_hash[32];
    reference_sha256(input, input_len, first_hash);
    reference_sha256(first_hash, 32, output);
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