#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <ArduinoJson.h>
#include "test_utils.h"
#include "fixtures/stratum_test_vectors.h"

// Only compile this for native tests
#ifdef NATIVE_TEST

// setUp and tearDown handled by main test file

//=============================================================================
// STRATUM PROTOCOL TESTS
//=============================================================================

// Test JSON parsing functionality
void test_json_parsing_basic(void) {
    DynamicJsonDocument doc(1024);

    // Test valid JSON parsing
    DeserializationError error = deserializeJson(doc, STRATUM_SUBSCRIBE_REQUEST);
    TEST_ASSERT_TRUE(error == DeserializationError::Ok);

    // Verify basic structure
    TEST_ASSERT_TRUE(doc.containsKey("id"));
    TEST_ASSERT_TRUE(doc.containsKey("method"));
    TEST_ASSERT_TRUE(doc.containsKey("params"));

    // Verify values
    TEST_ASSERT_EQUAL(1, doc["id"].as<int>());
    TEST_ASSERT_EQUAL_STRING("mining.subscribe", doc["method"].as<const char*>());
    TEST_ASSERT_TRUE(doc["params"].is<JsonArray>());
}

// Test invalid JSON handling
void test_json_parsing_invalid(void) {
    DynamicJsonDocument doc(1024);

    // Test each invalid JSON case
    for (int i = 0; INVALID_JSON_CASES[i] != NULL; i++) {
        const char* invalid_json = INVALID_JSON_CASES[i];
        DeserializationError error = deserializeJson(doc, invalid_json);

        char error_msg[128];
        sprintf(error_msg, "Invalid JSON case %d should fail: %s", i, invalid_json);
        TEST_ASSERT_FALSE_MESSAGE(error == DeserializationError::Ok, error_msg);

        doc.clear(); // Clear for next iteration
    }
}

// Test Stratum subscribe message parsing
void test_stratum_subscribe_parsing(void) {
    DynamicJsonDocument doc(1024);

    // Parse subscribe request
    DeserializationError error = deserializeJson(doc, STRATUM_SUBSCRIBE_REQUEST);
    TEST_ASSERT_TRUE(error == DeserializationError::Ok);

    // Validate structure
    TEST_ASSERT_EQUAL_STRING(STRATUM_METHOD_SUBSCRIBE, doc["method"].as<const char*>());
    TEST_ASSERT_TRUE(doc["params"].is<JsonArray>());

    JsonArray params = doc["params"];
    TEST_ASSERT_EQUAL(1, params.size());
    TEST_ASSERT_EQUAL_STRING("nerdminer/1.0", params[0].as<const char*>());
}

// Test Stratum authorize message parsing
void test_stratum_authorize_parsing(void) {
    DynamicJsonDocument doc(1024);

    // Parse authorize request
    DeserializationError error = deserializeJson(doc, STRATUM_AUTHORIZE_REQUEST);
    TEST_ASSERT_TRUE(error == DeserializationError::Ok);

    // Validate structure
    TEST_ASSERT_EQUAL_STRING(STRATUM_METHOD_AUTHORIZE, doc["method"].as<const char*>());
    TEST_ASSERT_TRUE(doc["params"].is<JsonArray>());

    JsonArray params = doc["params"];
    TEST_ASSERT_EQUAL(2, params.size());
    TEST_ASSERT_EQUAL_STRING("test_user.worker1", params[0].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("password", params[1].as<const char*>());
}

// Test Stratum notify message parsing (complex case)
void test_stratum_notify_parsing(void) {
    DynamicJsonDocument doc(2048); // Larger buffer for complex notify

    // Parse notify message
    DeserializationError error = deserializeJson(doc, STRATUM_NOTIFY_COMPLEX);
    TEST_ASSERT_TRUE(error == DeserializationError::Ok);

    // Validate structure
    TEST_ASSERT_EQUAL_STRING(STRATUM_METHOD_NOTIFY, doc["method"].as<const char*>());
    TEST_ASSERT_TRUE(doc["params"].is<JsonArray>());

    JsonArray params = doc["params"];
    TEST_ASSERT_EQUAL(9, params.size()); // job_id, prev_hash, coinb1, coinb2, merkle_branches, version, nbits, ntime, clean_jobs

    // Validate job_id
    TEST_ASSERT_EQUAL_STRING("job_id_complex", params[0].as<const char*>());

    // Validate prev_block_hash
    const char* prev_hash = params[1].as<const char*>();
    TEST_ASSERT_EQUAL(64, strlen(prev_hash)); // Should be 64 hex characters

    // Validate merkle branches
    TEST_ASSERT_TRUE(params[4].is<JsonArray>());
    JsonArray merkle_branches = params[4];
    TEST_ASSERT_EQUAL(3, merkle_branches.size());

    // Validate each merkle branch is 64 hex characters
    for (int i = 0; i < merkle_branches.size(); i++) {
        const char* branch = merkle_branches[i].as<const char*>();
        TEST_ASSERT_EQUAL(64, strlen(branch));
    }

    // Validate clean_jobs flag
    TEST_ASSERT_FALSE(params[8].as<bool>());
}

// Test Stratum submit message parsing
void test_stratum_submit_parsing(void) {
    DynamicJsonDocument doc(1024);

    // Parse submit request
    DeserializationError error = deserializeJson(doc, STRATUM_SUBMIT_REQUEST);
    TEST_ASSERT_TRUE(error == DeserializationError::Ok);

    // Validate structure
    TEST_ASSERT_EQUAL_STRING(STRATUM_METHOD_SUBMIT, doc["method"].as<const char*>());
    TEST_ASSERT_TRUE(doc["params"].is<JsonArray>());

    JsonArray params = doc["params"];
    TEST_ASSERT_EQUAL(5, params.size()); // worker, job_id, extranonce2, ntime, nonce

    // Validate worker name
    TEST_ASSERT_EQUAL_STRING("test_user.worker1", params[0].as<const char*>());

    // Validate job_id
    TEST_ASSERT_EQUAL_STRING("job_id_001", params[1].as<const char*>());
}

// Test Stratum response parsing (success cases)
void test_stratum_response_parsing(void) {
    DynamicJsonDocument doc(1024);

    // Test authorize success response
    DeserializationError error = deserializeJson(doc, STRATUM_AUTHORIZE_RESPONSE_SUCCESS);
    TEST_ASSERT_TRUE(error == DeserializationError::Ok);

    TEST_ASSERT_EQUAL(2, doc["id"].as<int>());
    TEST_ASSERT_TRUE(doc["result"].as<bool>());
    TEST_ASSERT_TRUE(doc["error"].isNull());

    doc.clear();

    // Test submit accepted response
    error = deserializeJson(doc, STRATUM_SUBMIT_RESPONSE_ACCEPTED);
    TEST_ASSERT_TRUE(error == DeserializationError::Ok);

    TEST_ASSERT_EQUAL(3, doc["id"].as<int>());
    TEST_ASSERT_TRUE(doc["result"].as<bool>());
    TEST_ASSERT_TRUE(doc["error"].isNull());
}

// Test Stratum error response parsing
void test_stratum_error_parsing(void) {
    DynamicJsonDocument doc(1024);

    // Test authorize failure response
    DeserializationError error = deserializeJson(doc, STRATUM_AUTHORIZE_RESPONSE_FAILURE);
    TEST_ASSERT_TRUE(error == DeserializationError::Ok);

    TEST_ASSERT_EQUAL(2, doc["id"].as<int>());
    TEST_ASSERT_FALSE(doc["result"].as<bool>());
    TEST_ASSERT_TRUE(doc["error"].is<JsonArray>());

    JsonArray error_array = doc["error"];
    TEST_ASSERT_EQUAL(3, error_array.size());
    TEST_ASSERT_EQUAL(21, error_array[0].as<int>()); // Error code
    TEST_ASSERT_EQUAL_STRING("Unauthorized worker", error_array[1].as<const char*>());

    doc.clear();

    // Test submit rejected response
    error = deserializeJson(doc, STRATUM_SUBMIT_RESPONSE_REJECTED);
    TEST_ASSERT_TRUE(error == DeserializationError::Ok);

    TEST_ASSERT_FALSE(doc["result"].as<bool>());
    TEST_ASSERT_TRUE(doc["error"].is<JsonArray>());

    error_array = doc["error"];
    TEST_ASSERT_EQUAL(23, error_array[0].as<int>()); // Low difficulty error code
    TEST_ASSERT_EQUAL_STRING("Low difficulty share", error_array[1].as<const char*>());
}

// Test hex string validation
void test_hex_string_validation(void) {
    // Valid hex strings
    TEST_ASSERT_TRUE(is_valid_hex_string("deadbeef"));
    TEST_ASSERT_TRUE(is_valid_hex_string("1234567890abcdef"));
    TEST_ASSERT_TRUE(is_valid_hex_string("DEADBEEF")); // Uppercase should be valid
    TEST_ASSERT_TRUE(is_valid_hex_string("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"));

    // Invalid hex strings
    TEST_ASSERT_FALSE(is_valid_hex_string("xyz123"));    // Contains non-hex chars
    TEST_ASSERT_FALSE(is_valid_hex_string("123g"));      // Contains 'g'
    TEST_ASSERT_FALSE(is_valid_hex_string(""));          // Empty string
    TEST_ASSERT_FALSE(is_valid_hex_string(NULL));        // NULL pointer
    TEST_ASSERT_FALSE(is_valid_hex_string("12345"));     // Odd length
}

// Test Stratum method validation
void test_stratum_method_validation(void) {
    // Valid methods
    TEST_ASSERT_TRUE(is_valid_stratum_method(STRATUM_METHOD_SUBSCRIBE));
    TEST_ASSERT_TRUE(is_valid_stratum_method(STRATUM_METHOD_AUTHORIZE));
    TEST_ASSERT_TRUE(is_valid_stratum_method(STRATUM_METHOD_NOTIFY));
    TEST_ASSERT_TRUE(is_valid_stratum_method(STRATUM_METHOD_SUBMIT));
    TEST_ASSERT_TRUE(is_valid_stratum_method(STRATUM_METHOD_SET_DIFFICULTY));

    // Invalid methods
    TEST_ASSERT_FALSE(is_valid_stratum_method("invalid.method"));
    TEST_ASSERT_FALSE(is_valid_stratum_method(""));
    TEST_ASSERT_FALSE(is_valid_stratum_method(NULL));
    TEST_ASSERT_FALSE(is_valid_stratum_method("mining.invalid"));
}

// Test mining job structure validation
void test_mining_job_structure(void) {
    // Test valid jobs
    TEST_ASSERT_TRUE(validate_stratum_job(&TEST_JOB_SIMPLE));
    TEST_ASSERT_TRUE(validate_stratum_job(&TEST_JOB_COMPLEX));

    // Test NULL job
    TEST_ASSERT_FALSE(validate_stratum_job(NULL));
}

// Test subscribe structure validation
void test_subscribe_structure(void) {
    // Test valid subscribe data
    TEST_ASSERT_TRUE(validate_stratum_subscribe(&TEST_SUBSCRIBE_DATA));

    // Test NULL subscribe
    TEST_ASSERT_FALSE(validate_stratum_subscribe(NULL));
}

// Test message size limits
void test_message_size_limits(void) {
    // Test that our test vectors are within reasonable size limits
    TEST_ASSERT_TRUE(strlen(STRATUM_SUBSCRIBE_REQUEST) < MAX_STRATUM_MESSAGE_SIZE);
    TEST_ASSERT_TRUE(strlen(STRATUM_AUTHORIZE_REQUEST) < MAX_STRATUM_MESSAGE_SIZE);
    TEST_ASSERT_TRUE(strlen(STRATUM_NOTIFY_COMPLEX) < MAX_STRATUM_MESSAGE_SIZE);
    TEST_ASSERT_TRUE(strlen(STRATUM_SUBMIT_REQUEST) < MAX_STRATUM_MESSAGE_SIZE);
}

// Test error code constants
void test_error_codes(void) {
    // Validate error code constants are defined correctly
    TEST_ASSERT_EQUAL(20, STRATUM_ERROR_OTHER);
    TEST_ASSERT_EQUAL(21, STRATUM_ERROR_JOB_NOT_FOUND);
    TEST_ASSERT_EQUAL(22, STRATUM_ERROR_DUPLICATE_SHARE);
    TEST_ASSERT_EQUAL(23, STRATUM_ERROR_LOW_DIFFICULTY);
    TEST_ASSERT_EQUAL(24, STRATUM_ERROR_UNAUTHORIZED);
    TEST_ASSERT_EQUAL(25, STRATUM_ERROR_NOT_SUBSCRIBED);
}

#endif // NATIVE_TEST