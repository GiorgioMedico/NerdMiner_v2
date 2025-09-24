#ifndef MINING_TEST_VECTORS_H
#define MINING_TEST_VECTORS_H

#include <stdint.h>
#include <cstddef>

// Mining test vectors and constants

// Bitcoin mining difficulty constants
#define BITCOIN_MAX_TARGET_HEX "00000000FFFF0000000000000000000000000000000000000000000000000000"
#define BITCOIN_GENESIS_TARGET_HEX "00000000FFFF0000000000000000000000000000000000000000000000000000"

// Test nonce values
#define TEST_NONCE_1 0x12345678
#define TEST_NONCE_2 0x87654321
#define TEST_NONCE_3 0xDEADBEEF
#define TEST_NONCE_ZERO 0x00000000
#define TEST_NONCE_MAX 0xFFFFFFFF

// Test difficulty values
#define TEST_DIFFICULTY_EASY 0.00001
#define TEST_DIFFICULTY_MEDIUM 0.0001
#define TEST_DIFFICULTY_HARD 0.001
#define TEST_DIFFICULTY_VERY_HARD 1.0

// Mining job test data (real Bitcoin mining job structure)
struct test_mining_job {
    const char* job_id;
    const char* prev_block_hash;
    const char* coinb1;
    const char* coinb2;
    const char* nbits;
    const char* merkle_branches[8]; // Up to 8 merkle branches
    int merkle_branch_count;
    const char* version;
    uint32_t target;
    const char* ntime;
    bool clean_jobs;
};

// Test mining job 1 - Simplified test case
static const struct test_mining_job TEST_MINING_JOB_1 = {
    .job_id = "test_job_001",
    .prev_block_hash = "0000000000000000000000000000000000000000000000000000000000000000",
    .coinb1 = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff",
    .coinb2 = "ffffffff0100f2052a01000000434104d64bdfd09eb1c5fe295abdeb1dca4281be988b",
    .nbits = "1d00ffff",
    .merkle_branches = {
        "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a",
        "ab6467e7b4c5a3c7f5cb6b44e8b4d1f2a9e6c3d8e7f1a2b4c6d8e1f3a5b7c9d1",
        NULL  // Terminator
    },
    .merkle_branch_count = 2,
    .version = "01000000",
    .target = 0x1d00ffff,
    .ntime = "495fab29",
    .clean_jobs = false
};

// Test mining job 2 - More complex case
static const struct test_mining_job TEST_MINING_JOB_2 = {
    .job_id = "test_job_002",
    .prev_block_hash = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f",
    .coinb1 = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff",
    .coinb2 = "ffffffff0100f2052a01000000434104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac00000000",
    .nbits = "1d00ffff",
    .merkle_branches = {
        "982051fd1e4ba744bbbe680e1fee14677ba1a3c3540bf7b1cdb606e857233e0e",
        "7a2de85b87f0cc2aa9ac1e0d0e5e7c1e8d3c7b5a4e6f9d2c8b1a5e7f9c3d6e8a",
        "1f8e2d5c3b7a9e6f4d8c1b5a7e9f3d6c8a2e5d7b9f1c4e8a6d3b7f2e9c5d8a10",
        NULL  // Terminator
    },
    .merkle_branch_count = 3,
    .version = "01000000",
    .target = 0x1d00ffff,
    .ntime = "495fab29",
    .clean_jobs = true
};

// Test block header template
static const uint8_t TEST_BLOCK_HEADER_TEMPLATE[80] = {
    // Version (4 bytes) - 0x01000000 (little endian)
    0x01, 0x00, 0x00, 0x00,
    // Previous block hash (32 bytes) - Genesis block
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Merkle root (32 bytes) - Test merkle root
    0x3b, 0xa3, 0xed, 0xfd, 0x7a, 0x7b, 0x12, 0xb2, 0x7a, 0xc7, 0x2c, 0x3e, 0x67, 0x76, 0x8f, 0x61,
    0x7f, 0xc8, 0x1b, 0xc3, 0x88, 0x8a, 0x51, 0x32, 0x3a, 0x9f, 0xb8, 0xaa, 0x4b, 0x1e, 0x5e, 0x4a,
    // Timestamp (4 bytes) - 0x495fab29 (little endian)
    0x29, 0xab, 0x5f, 0x49,
    // Bits/Target (4 bytes) - 0x1d00ffff (little endian)
    0xff, 0xff, 0x00, 0x1d,
    // Nonce (4 bytes) - 0x7c2bac1d (little endian) - This will be varied during mining
    0x1d, 0xac, 0x2b, 0x7c
};

// Difficulty target examples (32 bytes each, big endian)
static const uint8_t DIFFICULTY_TARGET_EASY[32] = {
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,  // Very easy target
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

static const uint8_t DIFFICULTY_TARGET_MEDIUM[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00,  // Medium target
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t DIFFICULTY_TARGET_HARD[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,  // Hard target
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Pool target example (typically easier than network difficulty)
static const uint8_t POOL_TARGET_EXAMPLE[32] = {
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // Pool target (easier)
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

// Mining performance test constants
#define MINING_TEST_ITERATIONS 1000
#define MINING_PERFORMANCE_TIMEOUT_MS 5000
#define EXPECTED_MIN_HASHRATE_KH_S 1.0    // Minimum 1 KH/s expected
#define EXPECTED_MAX_HASHRATE_KH_S 50.0   // Maximum 50 KH/s realistic for ESP32

// Nonce range test constants
#define NONCE_RANGE_TEST_START 0x10000000
#define NONCE_RANGE_TEST_END   0x20000000
#define NONCE_STEP_TEST        1000

// Validation functions
bool validate_mining_job(const struct test_mining_job* job);
bool validate_block_header(const uint8_t* header);
bool validate_nonce_in_range(uint32_t nonce, uint32_t min_nonce, uint32_t max_nonce);
bool validate_difficulty_calculation(double difficulty, const uint8_t* target);

#endif // MINING_TEST_VECTORS_H