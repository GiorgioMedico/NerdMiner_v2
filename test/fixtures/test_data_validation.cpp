#include "sha256_test_vectors.h"
#include "mining_test_vectors.h"
#include "stratum_test_vectors.h"
#include "../test_utils.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

// Implementation of validation functions referenced in test fixtures

// Mining test vectors validation functions
bool validate_mining_job(const struct test_mining_job* job) {
    if (!job) return false;
    if (!job->job_id || strlen(job->job_id) == 0) return false;
    if (!job->prev_block_hash || strlen(job->prev_block_hash) != 64) return false;
    if (!job->coinb1 || !job->coinb2) return false;
    if (!job->nbits || strlen(job->nbits) != 8) return false;  // 4 bytes hex = 8 chars
    if (!job->version || strlen(job->version) != 8) return false;  // 4 bytes hex = 8 chars
    if (!job->ntime || strlen(job->ntime) != 8) return false;  // 4 bytes hex = 8 chars
    if (job->merkle_branch_count < 0 || job->merkle_branch_count > 8) return false;

    // Validate merkle branches
    for (int i = 0; i < job->merkle_branch_count; i++) {
        if (!job->merkle_branches[i] || strlen(job->merkle_branches[i]) != 64) {
            return false;  // Each merkle branch should be 32 bytes (64 hex chars)
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

// Stratum test vectors validation functions
bool validate_stratum_subscribe(const test_mining_subscribe* subscribe) {
    if (!subscribe) return false;
    if (!subscribe->sub_details || strlen(subscribe->sub_details) == 0) return false;
    if (!subscribe->extranonce1 || !is_valid_hex_string(subscribe->extranonce1)) return false;
    if (!subscribe->extranonce2 || !is_valid_hex_string(subscribe->extranonce2)) return false;
    if (subscribe->extranonce2_size <= 0 || subscribe->extranonce2_size > 16) return false;
    if (!subscribe->wName || strlen(subscribe->wName) == 0) return false;
    if (!subscribe->wPass) return false;  // Password can be empty but not NULL

    return true;
}

bool validate_stratum_job(const test_mining_job* job) {
    if (!job) return false;
    if (!job->job_id || strlen(job->job_id) == 0) return false;
    if (!job->prev_block_hash || !is_valid_hex_string(job->prev_block_hash)) return false;
    if (!job->coinb1 || !is_valid_hex_string(job->coinb1)) return false;
    if (!job->coinb2 || !is_valid_hex_string(job->coinb2)) return false;
    if (!job->nbits || !is_valid_hex_string(job->nbits)) return false;
    if (!job->version || !is_valid_hex_string(job->version)) return false;
    if (!job->ntime || !is_valid_hex_string(job->ntime)) return false;
    if (job->merkle_branch_count < 0 || job->merkle_branch_count > 8) return false;

    // Validate merkle branches
    for (int i = 0; i < job->merkle_branch_count; i++) {
        if (!job->merkle_branches[i] || !is_valid_hex_string(job->merkle_branches[i])) {
            return false;
        }
    }

    return true;
}

bool validate_json_string(const char* json_str) {
    if (!json_str || strlen(json_str) == 0) return false;

    // Basic JSON validation - check for opening and closing braces
    const char* first_brace = strchr(json_str, '{');
    const char* last_brace = strrchr(json_str, '}');

    return (first_brace != NULL && last_brace != NULL && last_brace > first_brace);
}

bool is_valid_hex_string(const char* hex_str) {
    if (!hex_str) return false;

    size_t len = strlen(hex_str);
    if (len == 0 || len % 2 != 0) return false;  // Must be even length

    for (size_t i = 0; i < len; i++) {
        if (!isxdigit(hex_str[i])) {
            return false;
        }
    }

    return true;
}

bool is_valid_stratum_method(const char* method) {
    if (!method) return false;

    return (strcmp(method, STRATUM_METHOD_SUBSCRIBE) == 0 ||
            strcmp(method, STRATUM_METHOD_AUTHORIZE) == 0 ||
            strcmp(method, STRATUM_METHOD_NOTIFY) == 0 ||
            strcmp(method, STRATUM_METHOD_SUBMIT) == 0 ||
            strcmp(method, STRATUM_METHOD_SET_DIFFICULTY) == 0 ||
            strcmp(method, STRATUM_METHOD_SET_EXTRANONCE) == 0);
}

void validate_test_vectors(void) {
    // Validate that all test vectors are properly formatted

    // SHA256 test vector validation
    TEST_ASSERT_NOT_NULL(SHA256_TV1_INPUT);
    TEST_ASSERT_NOT_NULL(SHA256_TV1_EXPECTED);
    TEST_ASSERT_EQUAL(64, strlen(SHA256_TV1_EXPECTED)); // 32 bytes * 2 hex chars

    TEST_ASSERT_NOT_NULL(SHA256_TV2_INPUT);
    TEST_ASSERT_NOT_NULL(SHA256_TV2_EXPECTED);
    TEST_ASSERT_EQUAL(64, strlen(SHA256_TV2_EXPECTED));

    // Bitcoin block header validation
    TEST_ASSERT_EQUAL(80, sizeof(BITCOIN_BLOCK_HEADER_TV1));

    // Difficulty target validation
    TEST_ASSERT_EQUAL(32, sizeof(DIFFICULTY_TARGET_TV1));
    TEST_ASSERT_EQUAL(32, sizeof(DIFFICULTY_TARGET_TV2));

    // Mining job validation
    TEST_ASSERT_TRUE(validate_mining_job(&TEST_MINING_JOB_1));
    TEST_ASSERT_TRUE(validate_mining_job(&TEST_MINING_JOB_2));

    // Stratum data validation
    TEST_ASSERT_TRUE(validate_stratum_subscribe(&TEST_SUBSCRIBE_DATA));
    TEST_ASSERT_TRUE(validate_stratum_job(&TEST_JOB_SIMPLE));
    TEST_ASSERT_TRUE(validate_stratum_job(&TEST_JOB_COMPLEX));

    printf("All test vectors validated successfully\n");
}