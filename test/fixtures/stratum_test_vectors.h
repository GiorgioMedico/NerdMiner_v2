#ifndef STRATUM_TEST_VECTORS_H
#define STRATUM_TEST_VECTORS_H

#include <stdint.h>
#include <cstddef>

// Stratum Protocol Test Vectors
// Based on Bitcoin Stratum V1 protocol specification

// Stratum method names
#define STRATUM_METHOD_SUBSCRIBE "mining.subscribe"
#define STRATUM_METHOD_AUTHORIZE "mining.authorize"
#define STRATUM_METHOD_NOTIFY "mining.notify"
#define STRATUM_METHOD_SUBMIT "mining.submit"
#define STRATUM_METHOD_SET_DIFFICULTY "mining.set_difficulty"
#define STRATUM_METHOD_SET_EXTRANONCE "mining.set_extranonce"

// Test mining.subscribe request
static const char* STRATUM_SUBSCRIBE_REQUEST =
    "{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[\"nerdminer/1.0\"]}";

// Test mining.subscribe response
static const char* STRATUM_SUBSCRIBE_RESPONSE =
    "{\"id\":1,\"result\":[[\"mining.set_difficulty\",\"subscription_id_1\"],"
    "[\"mining.notify\",\"subscription_id_2\"],\"extranonce1_hex\",4],\"error\":null}";

// Test mining.authorize request
static const char* STRATUM_AUTHORIZE_REQUEST =
    "{\"id\":2,\"method\":\"mining.authorize\",\"params\":[\"test_user.worker1\",\"password\"]}";

// Test mining.authorize response (success)
static const char* STRATUM_AUTHORIZE_RESPONSE_SUCCESS =
    "{\"id\":2,\"result\":true,\"error\":null}";

// Test mining.authorize response (failure)
static const char* STRATUM_AUTHORIZE_RESPONSE_FAILURE =
    "{\"id\":2,\"result\":false,\"error\":[21,\"Unauthorized worker\",null]}";

// Test mining.notify message
static const char* STRATUM_NOTIFY_MESSAGE =
    "{\"id\":null,\"method\":\"mining.notify\",\"params\":["
    "\"job_id_001\","
    "\"prev_block_hash_hex\","
    "\"coinb1_hex\","
    "\"coinb2_hex\","
    "[\"merkle_branch_1\",\"merkle_branch_2\"],"
    "\"version_hex\","
    "\"nbits_hex\","
    "\"ntime_hex\","
    "true]}";

// Test mining.submit request
static const char* STRATUM_SUBMIT_REQUEST =
    "{\"id\":3,\"method\":\"mining.submit\",\"params\":["
    "\"test_user.worker1\","
    "\"job_id_001\","
    "\"extranonce2_hex\","
    "\"ntime_hex\","
    "\"nonce_hex\"]}";

// Test mining.submit response (accepted)
static const char* STRATUM_SUBMIT_RESPONSE_ACCEPTED =
    "{\"id\":3,\"result\":true,\"error\":null}";

// Test mining.submit response (rejected)
static const char* STRATUM_SUBMIT_RESPONSE_REJECTED =
    "{\"id\":3,\"result\":false,\"error\":[23,\"Low difficulty share\",null]}";

// Test mining.set_difficulty message
static const char* STRATUM_SET_DIFFICULTY_MESSAGE =
    "{\"id\":null,\"method\":\"mining.set_difficulty\",\"params\":[0.25]}";

// Complex mining.notify with multiple merkle branches
static const char* STRATUM_NOTIFY_COMPLEX =
    "{\"id\":null,\"method\":\"mining.notify\",\"params\":["
    "\"job_id_complex\","
    "\"000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f\","
    "\"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff\","
    "\"ffffffff0100f2052a01000000434104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac00000000\","
    "[\"982051fd1e4ba744bbbe680e1fee14677ba1a3c3540bf7b1cdb606e857233e0e\","
    "\"7a2de85b87f0cc2aa9ac1e0d0e5e7c1e8d3c7b5a4e6f9d2c8b1a5e7f9c3d6e8a\","
    "\"1f8e2d5c3b7a9e6f4d8c1b5a7e9f3d6c8a2e5d7b9f1c4e8a6d3b7f2e9c5d8a1f\"],"
    "\"01000000\","
    "\"1d00ffff\","
    "\"495fab29\","
    "false]}";

// Error response examples
static const char* STRATUM_ERROR_INVALID_JSON =
    "{\"id\":1,\"result\":null,\"error\":[20,\"Other/Unknown\",\"Invalid JSON\"]}";

static const char* STRATUM_ERROR_METHOD_NOT_FOUND =
    "{\"id\":1,\"result\":null,\"error\":[1,\"Method not found\",null]}";

static const char* STRATUM_ERROR_INVALID_PARAMS =
    "{\"id\":1,\"result\":null,\"error\":[2,\"Invalid params\",null]}";

// Test data structures matching stratum.h
typedef struct {
    const char* sub_details;
    const char* extranonce1;
    const char* extranonce2;
    int extranonce2_size;
    const char* wName;
    const char* wPass;
} test_mining_subscribe;

typedef struct {
    const char* job_id;
    const char* prev_block_hash;
    const char* coinb1;
    const char* coinb2;
    const char* nbits;
    const char* merkle_branches[8]; // Max 8 branches for testing
    int merkle_branch_count;
    const char* version;
    uint32_t target;
    const char* ntime;
    bool clean_jobs;
} test_stratum_mining_job;

// Test mining subscribe data
static const test_mining_subscribe TEST_SUBSCRIBE_DATA = {
    .sub_details = "nerdminer/1.0",
    .extranonce1 = "f0000000",
    .extranonce2 = "00000000",
    .extranonce2_size = 4,
    .wName = "test_user.worker1",
    .wPass = "password"
};

// Test mining job data
static const test_stratum_mining_job TEST_JOB_SIMPLE = {
    .job_id = "job_id_001",
    .prev_block_hash = "0000000000000000000000000000000000000000000000000000000000000000",
    .coinb1 = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff",
    .coinb2 = "ffffffff0100f2052a01000000434104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac00000000",
    .nbits = "1d00ffff",
    .merkle_branches = {"3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a", "ab6467e7b4c5a3c7f5cb6b44e8b4d1f2a9e6c3d8e7f1a2b4c6d8e1f3a5b7c9d1", NULL},
    .merkle_branch_count = 2,
    .version = "01000000",
    .target = 0x1d00ffff,
    .ntime = "495fab29",
    .clean_jobs = true
};

static const test_stratum_mining_job TEST_JOB_COMPLEX = {
    .job_id = "job_id_complex",
    .prev_block_hash = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f",
    .coinb1 = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff",
    .coinb2 = "ffffffff0100f2052a01000000434104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac00000000",
    .nbits = "1d00ffff",
    .merkle_branches = {
        "982051fd1e4ba744bbbe680e1fee14677ba1a3c3540bf7b1cdb606e857233e0e",
        "7a2de85b87f0cc2aa9ac1e0d0e5e7c1e8d3c7b5a4e6f9d2c8b1a5e7f9c3d6e8a",
        "1f8e2d5c3b7a9e6f4d8c1b5a7e9f3d6c8a2e5d7b9f1c4e8a6d3b7f2e9c5d8a1f",
        NULL
    },
    .merkle_branch_count = 3,
    .version = "01000000",
    .target = 0x1d00ffff,
    .ntime = "495fab29",
    .clean_jobs = false
};

// Invalid/malformed JSON test cases
static const char* INVALID_JSON_CASES[] = {
    "{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[}", // Missing closing bracket
    "not valid json at all", // Not JSON
    "", // Empty string
    NULL // NULL pointer terminator
};

// Stratum error codes (from Bitcoin Stratum specification)
#define STRATUM_ERROR_OTHER 20
#define STRATUM_ERROR_JOB_NOT_FOUND 21
#define STRATUM_ERROR_DUPLICATE_SHARE 22
#define STRATUM_ERROR_LOW_DIFFICULTY 23
#define STRATUM_ERROR_UNAUTHORIZED 24
#define STRATUM_ERROR_NOT_SUBSCRIBED 25

// Test helper constants
#define MAX_STRATUM_MESSAGE_SIZE 2048
#define MAX_JOB_ID_LENGTH 64
#define MAX_EXTRANONCE_LENGTH 16
#define MAX_MERKLE_BRANCHES 32

// Validation function implementations
inline bool validate_stratum_subscribe(const test_mining_subscribe* subscribe) {
    if (!subscribe) return false;
    if (!subscribe->sub_details || strlen(subscribe->sub_details) == 0) return false;
    if (!subscribe->extranonce1 || strlen(subscribe->extranonce1) == 0) return false;
    if (!subscribe->wName || strlen(subscribe->wName) == 0) return false;
    if (!subscribe->wPass) return false; // Password can be empty but not NULL
    if (subscribe->extranonce2_size <= 0 || subscribe->extranonce2_size > MAX_EXTRANONCE_LENGTH) return false;

    return true;
}

inline bool validate_stratum_job(const test_stratum_mining_job* job) {
    if (!job) return false;
    if (!job->job_id || strlen(job->job_id) == 0) return false;
    if (!job->prev_block_hash || strlen(job->prev_block_hash) != 64) return false; // 32 bytes = 64 hex chars
    if (!job->coinb1 || !job->coinb2) return false;
    if (!job->nbits || strlen(job->nbits) != 8) return false; // 4 bytes = 8 hex chars
    if (!job->version || strlen(job->version) != 8) return false; // 4 bytes = 8 hex chars
    if (!job->ntime || strlen(job->ntime) != 8) return false; // 4 bytes = 8 hex chars
    if (job->merkle_branch_count < 0 || job->merkle_branch_count > 8) return false;

    // Validate merkle branches
    for (int i = 0; i < job->merkle_branch_count; i++) {
        if (!job->merkle_branches[i] || strlen(job->merkle_branches[i]) != 64) {
            return false; // Each merkle branch should be 32 bytes = 64 hex chars
        }
    }

    return true;
}

#endif // STRATUM_TEST_VECTORS_H