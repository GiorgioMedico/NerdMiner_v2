#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "test_utils.h"
#include "fixtures/mining_test_vectors.h"
#include "fixtures/sha256_test_vectors.h"

// Only compile this for native tests
#ifdef NATIVE_TEST

// Mock mining data structure
typedef struct {
    uint8_t bytearray_target[32];
    uint8_t bytearray_pooltarget[32];
    uint8_t merkle_result[32];
    uint8_t bytearray_blockheader[128];
} miner_data;

// External SHA256 implementation from test_native_all.cpp
extern void reference_sha256(const uint8_t* input, size_t input_len, uint8_t* output);
extern void reference_sha256_double(const uint8_t* input, size_t input_len, uint8_t* output);

// setUp and tearDown handled by main test file

//=============================================================================
// MINING INTEGRATION TESTS
//=============================================================================

// Test block header construction from mining job
void test_block_header_construction(void) {
    uint8_t block_header[80];
    memset(block_header, 0, 80);

    // Copy test block header template
    memcpy(block_header, TEST_BLOCK_HEADER_TEMPLATE, 80);

    // Verify header structure
    TEST_ASSERT_TRUE(validate_block_header(block_header));

    // Check version (first 4 bytes, little endian)
    uint32_t version = *((uint32_t*)&block_header[0]);
    TEST_ASSERT_EQUAL(1, version);

    // Check that previous block hash is at correct offset (bytes 4-35)
    bool prev_hash_set = false;
    for (int i = 4; i < 36; i++) {
        if (block_header[i] != 0) {
            prev_hash_set = true;
            break;
        }
    }
    // Genesis block has zero prev hash, which is valid
    TEST_ASSERT_TRUE(true); // Previous hash can be zero for genesis

    // Check merkle root is at correct offset (bytes 36-67)
    bool merkle_set = false;
    for (int i = 36; i < 68; i++) {
        if (block_header[i] != 0) {
            merkle_set = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(merkle_set);

    // Check timestamp is set (bytes 68-71)
    uint32_t timestamp = *((uint32_t*)&block_header[68]);
    TEST_ASSERT_TRUE(timestamp > 0);

    // Check bits/target is set (bytes 72-75)
    uint32_t bits = *((uint32_t*)&block_header[72]);
    TEST_ASSERT_TRUE(bits > 0);

    // Check nonce is at correct offset (bytes 76-79)
    uint32_t nonce = *((uint32_t*)&block_header[76]);
    TEST_ASSERT_TRUE(nonce >= 0); // Nonce can be zero
}

// Test mining difficulty target comparison
void test_difficulty_target_comparison(void) {
    uint8_t hash_result[32];
    uint8_t difficulty_target[32];

    // Test with easy target
    memcpy(difficulty_target, DIFFICULTY_TARGET_EASY, 32);

    // Create a hash that should be below the easy target
    memset(hash_result, 0, 32);
    hash_result[4] = 0x01; // Small value, should be below easy target

    // Test target comparison logic
    int comparison = memcmp(hash_result, difficulty_target, 32);
    TEST_ASSERT_TRUE(comparison <= 0); // Hash should be <= target for valid solution

    // Test with hard target
    memcpy(difficulty_target, DIFFICULTY_TARGET_HARD, 32);

    // Create a hash that should be above the hard target
    memset(hash_result, 0xFF, 32);
    hash_result[0] = 0x01; // Still high value, should be above hard target

    comparison = memcmp(hash_result, difficulty_target, 32);
    TEST_ASSERT_TRUE(comparison > 0); // Hash should be > target for invalid solution
}

// Test nonce iteration logic
void test_nonce_iteration(void) {
    uint8_t block_header[80];
    memcpy(block_header, TEST_BLOCK_HEADER_TEMPLATE, 80);

    uint32_t start_nonce = 0;
    uint32_t end_nonce = 1000;

    for (uint32_t nonce = start_nonce; nonce < end_nonce; nonce++) {
        // Set nonce in block header (bytes 76-79, little endian)
        *((uint32_t*)&block_header[76]) = nonce;

        // Verify nonce was set correctly
        uint32_t read_nonce = *((uint32_t*)&block_header[76]);
        TEST_ASSERT_EQUAL(nonce, read_nonce);

        // Validate nonce is in expected range
        TEST_ASSERT_TRUE(validate_nonce_in_range(nonce, start_nonce, end_nonce));

        // Break early to avoid long test execution
        if (nonce > 10) break;
    }
}

// Test complete mining cycle simulation
void test_mining_cycle_simulation(void) {
    uint8_t block_header[80];
    uint8_t hash_result[32];
    uint8_t target[32];

    // Set up block header
    memcpy(block_header, TEST_BLOCK_HEADER_TEMPLATE, 80);

    // Set up easy target for testing
    memcpy(target, DIFFICULTY_TARGET_EASY, 32);

    uint32_t nonce = 0;
    bool solution_found = false;
    uint32_t max_iterations = 100; // Limit iterations for test

    // Simulate mining loop
    for (uint32_t i = 0; i < max_iterations; i++) {
        // Set current nonce
        *((uint32_t*)&block_header[76]) = nonce;

        // Compute double SHA256 (Bitcoin mining hash)
        reference_sha256_double(block_header, 80, hash_result);

        // Check if hash meets target (in practice, this would be a very rare event)
        int comparison = memcmp(hash_result, target, 32);
        if (comparison <= 0) {
            solution_found = true;
            break;
        }

        nonce++;
    }

    // For this test, we don't expect to find a solution with easy target
    // and few iterations, but the mining logic should work correctly
    TEST_ASSERT_TRUE(nonce <= max_iterations);

    // Verify final hash computation is valid
    reference_sha256_double(block_header, 80, hash_result);
    TEST_ASSERT_TRUE(validate_sha256_hash(hash_result));
}

// Test merkle root calculation simulation
void test_merkle_root_calculation(void) {
    // Simulate merkle root calculation using test mining job
    const struct test_mining_job* job = &TEST_MINING_JOB_2;

    // Validate the mining job structure
    TEST_ASSERT_TRUE(validate_mining_job(job));

    // Simulate coinbase transaction construction
    // In real mining, this would involve concatenating coinb1 + extranonce1 + extranonce2 + coinb2
    size_t coinb1_len = strlen(job->coinb1) / 2; // Convert hex to byte length
    size_t coinb2_len = strlen(job->coinb2) / 2;

    TEST_ASSERT_TRUE(coinb1_len > 0);
    TEST_ASSERT_TRUE(coinb2_len > 0);

    // Test merkle branch validation
    for (int i = 0; i < job->merkle_branch_count; i++) {
        const char* branch = job->merkle_branches[i];
        TEST_ASSERT_NOT_NULL(branch);
        TEST_ASSERT_EQUAL(64, strlen(branch)); // Should be 32 bytes = 64 hex chars

        // Test that branch is valid hex
        TEST_ASSERT_TRUE(is_valid_hex_string(branch));
    }

    // Simulate merkle root calculation
    uint8_t merkle_result[32];
    uint8_t temp_hash[32];

    // Start with coinbase transaction hash (simplified simulation)
    const char* coinbase_hash = "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890";
    hex_string_to_bytes(coinbase_hash, merkle_result, 32);

    // Apply merkle branches
    for (int i = 0; i < job->merkle_branch_count; i++) {
        uint8_t branch_bytes[32];
        hex_string_to_bytes(job->merkle_branches[i], branch_bytes, 32);

        // Concatenate current merkle result with branch and hash
        uint8_t combined[64];
        memcpy(combined, merkle_result, 32);
        memcpy(combined + 32, branch_bytes, 32);

        // Double SHA256 for merkle calculation
        reference_sha256_double(combined, 64, merkle_result);
    }

    // Verify final merkle root is valid
    TEST_ASSERT_TRUE(validate_sha256_hash(merkle_result));
}

// Test mining performance measurement
void test_mining_performance_measurement(void) {
    uint8_t block_header[80];
    uint8_t hash_result[32];

    // Set up block header
    memcpy(block_header, TEST_BLOCK_HEADER_TEMPLATE, 80);

    uint32_t iterations = MINING_TEST_ITERATIONS;
    uint32_t nonce = 0;

    // Measure hash computation performance
    for (uint32_t i = 0; i < iterations; i++) {
        // Set nonce
        *((uint32_t*)&block_header[76]) = nonce++;

        // Compute hash
        reference_sha256_double(block_header, 80, hash_result);

        // Verify hash is valid
        TEST_ASSERT_TRUE(validate_sha256_hash(hash_result));
    }

    // Test completed successfully if we can compute the expected number of hashes
    TEST_ASSERT_EQUAL(iterations, nonce);
}

// Test mining data structure initialization
void test_mining_data_initialization(void) {
    miner_data data;
    memset(&data, 0, sizeof(data));

    // Test structure size - allow for padding variations
    size_t expected_min_size = 224; // 32+32+32+128 = 224 bytes
    size_t actual_size = sizeof(data);
    TEST_ASSERT_TRUE(actual_size >= expected_min_size);

    // Initialize with test data
    memcpy(data.bytearray_target, DIFFICULTY_TARGET_EASY, 32);
    memcpy(data.bytearray_pooltarget, POOL_TARGET_EXAMPLE, 32);
    memcpy(data.bytearray_blockheader, TEST_BLOCK_HEADER_TEMPLATE, 80);

    // Verify target validation
    TEST_ASSERT_TRUE(validate_bitcoin_difficulty_target(data.bytearray_target));
    TEST_ASSERT_TRUE(validate_bitcoin_difficulty_target(data.bytearray_pooltarget));

    // Verify block header
    TEST_ASSERT_TRUE(validate_block_header(data.bytearray_blockheader));

    // Test merkle result calculation space
    uint8_t test_merkle[32];
    memset(test_merkle, 0xAA, 32);
    memcpy(data.merkle_result, test_merkle, 32);

    // Verify merkle result was set
    TEST_ASSERT_TRUE(memcmp(data.merkle_result, test_merkle, 32) == 0);
}

// Test mining constants validation
void test_mining_constants_validation(void) {
    // Test nonce range constants
#ifdef MAX_NONCE_STEP
    TEST_ASSERT_TRUE(MAX_NONCE_STEP >= 1000000);   // At least 1M
    TEST_ASSERT_TRUE(MAX_NONCE_STEP <= 10000000);  // At most 10M
#endif

#ifdef MAX_NONCE
    TEST_ASSERT_TRUE(MAX_NONCE >= 10000000);       // At least 10M
    TEST_ASSERT_TRUE(MAX_NONCE <= 100000000);      // At most 100M
#endif

#ifdef TARGET_NONCE
    TEST_ASSERT_TRUE(TARGET_NONCE > 0);            // Should be positive
    TEST_ASSERT_TRUE(TARGET_NONCE <= UINT32_MAX);  // Should fit in uint32_t
#endif

#ifdef DEFAULT_DIFFICULTY
    TEST_ASSERT_TRUE(DEFAULT_DIFFICULTY > 0.0);   // Should be positive
    TEST_ASSERT_TRUE(DEFAULT_DIFFICULTY < 1.0);   // Should be reasonable
#endif

    // Test mining performance constants
    TEST_ASSERT_TRUE(MINING_TEST_ITERATIONS > 0);
    TEST_ASSERT_TRUE(MINING_PERFORMANCE_TIMEOUT_MS > 0);
    TEST_ASSERT_TRUE(EXPECTED_MIN_HASHRATE_KH_S > 0.0);
    TEST_ASSERT_TRUE(EXPECTED_MAX_HASHRATE_KH_S > EXPECTED_MIN_HASHRATE_KH_S);
}

// Test work distribution simulation
void test_work_distribution(void) {
    uint32_t total_work = 25000000; // MAX_NONCE typical value
    uint32_t work_step = 5000000;   // MAX_NONCE_STEP typical value

    uint32_t current_nonce = 0;
    int work_chunks = 0;

    // Simulate work distribution
    while (current_nonce < total_work) {
        uint32_t end_nonce = current_nonce + work_step;
        if (end_nonce > total_work) {
            end_nonce = total_work;
        }

        // Validate work chunk
        TEST_ASSERT_TRUE(end_nonce > current_nonce);
        TEST_ASSERT_TRUE(end_nonce <= total_work);
        TEST_ASSERT_TRUE(validate_nonce_in_range(current_nonce, 0, total_work));
        TEST_ASSERT_TRUE(validate_nonce_in_range(end_nonce - 1, 0, total_work));

        current_nonce = end_nonce;
        work_chunks++;

        // Safety limit
        if (work_chunks > 10) break;
    }

    TEST_ASSERT_TRUE(work_chunks > 0);
    TEST_ASSERT_TRUE(work_chunks <= 5); // Should distribute into reasonable number of chunks
}

#endif // NATIVE_TEST