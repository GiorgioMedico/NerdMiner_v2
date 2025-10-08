#include <unity.h>
#include <Arduino.h>

// Include the actual source files for testing
#include "../../src/utils.cpp"

#ifdef MINING_TEST
#include "../../src/mining.h"
// Include source files needed for mining tests
// Note: This requires WiFi and other dependencies
#endif

#ifdef STRATUM_TEST
#include "../../src/stratum.h"
// Include stratum source for testing
#include "../../src/stratum.cpp"
#endif

#ifdef SHA256_TEST
#include <mbedtls/sha256.h>
#endif

void setUp(void) {
    // Set up function called before each test
    Serial.begin(115200);
    delay(100);
}

void tearDown(void) {
    // Tear down function called after each test
}

// =============================================================================
// COMMON UTILITY TESTS (used by all test modes)
// =============================================================================

void test_hex_conversion() {
    TEST_ASSERT_EQUAL_UINT8(0, hex('0'));
    TEST_ASSERT_EQUAL_UINT8(9, hex('9'));
    TEST_ASSERT_EQUAL_UINT8(10, hex('a'));
    TEST_ASSERT_EQUAL_UINT8(10, hex('A'));
    TEST_ASSERT_EQUAL_UINT8(15, hex('f'));
    TEST_ASSERT_EQUAL_UINT8(15, hex('F'));
    TEST_ASSERT_EQUAL_UINT8(0, hex('g'));

    Serial.println("✓ Hex conversion tests passed");
}

void test_to_byte_array() {
    const char* hex_string = "deadbeef";
    uint8_t expected[] = {0xde, 0xad, 0xbe, 0xef};
    uint8_t result[4];

    int converted = to_byte_array(hex_string, sizeof(result), result);

    TEST_ASSERT_EQUAL_INT(4, converted);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, result, 4);

    const char* odd_hex_string = "abc";
    uint8_t odd_expected[] = {0x0a, 0xbc};
    uint8_t odd_result[2] = {0};
    int odd_converted = to_byte_array(odd_hex_string, sizeof(odd_result), odd_result);

    TEST_ASSERT_EQUAL_INT(2, odd_converted);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(odd_expected, odd_result, 2);

    uint8_t truncated_result[1];
    int truncated_converted = to_byte_array(hex_string, sizeof(truncated_result), truncated_result);

    TEST_ASSERT_EQUAL_INT(1, truncated_converted);
    TEST_ASSERT_EQUAL_UINT8(0xde, truncated_result[0]);

    TEST_ASSERT_EQUAL_INT(0, to_byte_array(nullptr, sizeof(result), result));
    TEST_ASSERT_EQUAL_INT(0, to_byte_array(hex_string, 0, result));

    Serial.println("✓ Byte array conversion tests passed");
}

void test_crc32_functions() {
    uint32_t crc = crc32_reset();
    const char* test_data = "Hello, World!";

    crc = crc32_add(crc, test_data, strlen(test_data));
    uint32_t final_crc = crc32_finish(crc);

    // CRC32 with 0xFFFFFFFF seed should match ESP ROM helper behaviour
    TEST_ASSERT_EQUAL_UINT32(0xE33E8552, final_crc);

    uint32_t same_crc = crc32_finish(crc32_add(crc32_reset(), test_data, strlen(test_data)));
    TEST_ASSERT_EQUAL_UINT32(final_crc, same_crc);

    uint32_t zero_len_crc = crc32_add(crc32_reset(), test_data, 0);
    TEST_ASSERT_EQUAL_UINT32(crc32_reset(), zero_len_crc);

    uint32_t null_crc = crc32_add(crc32_reset(), nullptr, strlen(test_data));
    TEST_ASSERT_EQUAL_UINT32(crc32_reset(), null_crc);


    Serial.println("✓ CRC32 function tests passed");
}

void test_suffix_string() {
    char buffer[32];

    suffix_string(999.0, buffer, sizeof(buffer), 0);
    TEST_ASSERT_EQUAL_STRING("999.00", buffer);

    suffix_string(1000.0, buffer, sizeof(buffer), 0);
    TEST_ASSERT_EQUAL_STRING("1.000K", buffer);

    suffix_string(1500000.0, buffer, sizeof(buffer), 0);
    TEST_ASSERT_EQUAL_STRING("1.500M", buffer);

    suffix_string(0.0005, buffer, sizeof(buffer), 0);
    TEST_ASSERT_EQUAL_STRING("0.0000", buffer);

    suffix_string(123.0, buffer, sizeof(buffer), 3);
    TEST_ASSERT_EQUAL_STRING(" 123", buffer);

    char tiny_buffer[4];
    memset(tiny_buffer, 'x', sizeof(tiny_buffer));
    suffix_string(1000.0, tiny_buffer, sizeof(tiny_buffer), 0);
    TEST_ASSERT_EQUAL_UINT8('\0', tiny_buffer[0]);

    Serial.println("✓ Suffix string tests passed");
}

void test_sha256_validation_basic() {
    unsigned char valid_hash[32] = {0};
    unsigned char non_zero_hash[32];
    unsigned char zero_hash[32] = {0};

    valid_hash[31] = 0x01;
    memset(non_zero_hash, 0xff, sizeof(non_zero_hash));

    TEST_ASSERT_TRUE(isSha256Valid(valid_hash));
    TEST_ASSERT_TRUE(isSha256Valid(non_zero_hash));
    TEST_ASSERT_FALSE(isSha256Valid(zero_hash));

    Serial.println("✓ SHA256 validation tests passed");
}

// =============================================================================
// MINING TESTS (only when MINING_TEST is defined)
// =============================================================================

#ifdef MINING_TEST

void test_mining_nonce_configuration() {
    Serial.println("\n=== Testing Mining Nonce Configuration ===");

    // MAX_NONCE must be a positive multiple of the step size so workers partition cleanly
    TEST_ASSERT_TRUE(MAX_NONCE_STEP > 0);
    TEST_ASSERT_TRUE(MAX_NONCE >= MAX_NONCE_STEP);
    TEST_ASSERT_EQUAL_UINT32(0, MAX_NONCE % MAX_NONCE_STEP);

    // Keep-alive must fire before the pool inactivity threshold to maintain the socket
    TEST_ASSERT_TRUE(KEEPALIVE_TIME_ms < POOLINACTIVITY_TIME_ms);

    // Random nonce start must leave us enough headroom to scan MAX_NONCE nonces without overflow
    TEST_ASSERT_TRUE(UINT32_MAX - NONCE_START_RANDOM >= MAX_NONCE);

    Serial.println("✓ Mining nonce configuration validated");
}

void test_mining_calculate_mining_data_core_fields() {
    Serial.println("\n=== Testing Mining Data Preparation ===");

    mining_subscribe worker;
    worker.extranonce1 = "00000000";
    worker.extranonce2_size = 4;
    strcpy(worker.wName, "test_worker");
    strcpy(worker.wPass, "x");

    mining_job job;
    job.merkle_branch_len = 0;
    job.job_id = "job";
    job.prev_block_hash = "0000000000000000000000000000000000000000000000000000000000000000";
    job.coinb1 = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff08";
    job.coinb2 = "ffffffff0100f2052a01000000434104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac00000000";
    job.version = "00000001";
    job.nbits = "1d00ffff";
    job.ntime = "4c92809d";
    job.clean_jobs = true;

    miner_data prepared = calculateMiningData(worker, job);

    const uint8_t expected_target[32] = {
        0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_target, prepared.bytearray_target, sizeof(expected_target));

    // Block header version should be little-endian (0x00000001 -> 01 00 00 00)
    TEST_ASSERT_EQUAL_UINT8(0x01, prepared.bytearray_blockheader[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00, prepared.bytearray_blockheader[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00, prepared.bytearray_blockheader[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00, prepared.bytearray_blockheader[3]);

    // Coinbase hashing should produce a non-zero merkle root
    TEST_ASSERT_TRUE(isSha256Valid(prepared.merkle_result));

    // Pool target is not populated in calculateMiningData yet; it should remain zeroed
    for (size_t i = 0; i < sizeof(prepared.bytearray_pooltarget); ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, prepared.bytearray_pooltarget[i]);
    }

    Serial.println("✓ Mining data preparation verified");
}

void test_miner_data_structure() {
    Serial.println("\n=== Testing Miner Data Structure ===");

    miner_data miner;

    // Test structure sizes
    TEST_ASSERT_EQUAL_size_t(32, sizeof(miner.bytearray_target));
    TEST_ASSERT_EQUAL_size_t(32, sizeof(miner.bytearray_pooltarget));
    TEST_ASSERT_EQUAL_size_t(32, sizeof(miner.merkle_result));
    TEST_ASSERT_EQUAL_size_t(128, sizeof(miner.bytearray_blockheader));

    // Initialize with test data
    memset(miner.bytearray_target, 0xAA, sizeof(miner.bytearray_target));
    memset(miner.bytearray_pooltarget, 0xBB, sizeof(miner.bytearray_pooltarget));
    memset(miner.merkle_result, 0xCC, sizeof(miner.merkle_result));
    memset(miner.bytearray_blockheader, 0xDD, sizeof(miner.bytearray_blockheader));

    // Verify data integrity
    TEST_ASSERT_EQUAL_UINT8(0xAA, miner.bytearray_target[0]);
    TEST_ASSERT_EQUAL_UINT8(0xAA, miner.bytearray_target[31]);
    TEST_ASSERT_EQUAL_UINT8(0xBB, miner.bytearray_pooltarget[15]);
    TEST_ASSERT_EQUAL_UINT8(0xCC, miner.merkle_result[10]);
    TEST_ASSERT_EQUAL_UINT8(0xDD, miner.bytearray_blockheader[127]);

    Serial.println("✓ Miner data structure tests passed");
}

#endif // MINING_TEST

// =============================================================================
// STRATUM TESTS (only when STRATUM_TEST is defined)
// =============================================================================

#ifdef STRATUM_TEST

void test_stratum_get_next_id() {
    Serial.println("\n=== Testing Stratum ID Generation ===");

    // Test 1: Basic functionality - getNextId returns current value and increments atomic
    Serial.println("  Test 1: Basic increment from 1");
    std::atomic<unsigned long> test_id(1);
    unsigned long returned_id = getNextId(test_id);
    TEST_ASSERT_EQUAL_UINT32(1, returned_id); // Returns the current value (1)
    TEST_ASSERT_EQUAL_UINT32(2, test_id.load()); // Atomic is incremented to 2
    Serial.println("    ✓ Returns 1, increments to 2");

    // Test 2: Test with different starting value
    Serial.println("  Test 2: Increment from 100");
    test_id = 100;
    returned_id = getNextId(test_id);
    TEST_ASSERT_EQUAL_UINT32(100, returned_id); // Returns current value (100)
    TEST_ASSERT_EQUAL_UINT32(101, test_id.load()); // Atomic is incremented to 101
    Serial.println("    ✓ Returns 100, increments to 101");

    // Test 3: Test wraparound at ULONG_MAX (unsigned long overflow)
    Serial.println("  Test 3: Wraparound at ULLONG_MAX");
    test_id = ULONG_MAX;
    returned_id = getNextId(test_id);
    TEST_ASSERT_EQUAL_UINT32(ULONG_MAX, returned_id); // Returns current value (ULLONG_MAX)
    TEST_ASSERT_EQUAL_UINT32(0, test_id.load()); // Atomic wraps to 0
    Serial.println("    ✓ Returns ULLONG_MAX, wraps to 0");

    Serial.println("✓ Stratum ID generation tests passed");
}

void test_stratum_verify_payload() {
    Serial.println("\n=== Testing Stratum Payload Verification ===");

    // Test valid payload
    String valid_payload = "{\"id\":1,\"method\":\"mining.notify\"}";
    bool result = verifyPayload(valid_payload.c_str());
    TEST_ASSERT_TRUE(result);

    // Test empty payload
    String empty_payload = "";
    result = verifyPayload(empty_payload.c_str());
    TEST_ASSERT_FALSE(result);

    // Test whitespace only payload
    String whitespace_payload = "   \t\n   ";
    result = verifyPayload(whitespace_payload.c_str());
    TEST_ASSERT_FALSE(result);

    // Test payload with leading/trailing whitespace
    String trimmed_payload = "  {\"id\":1}  ";
    result = verifyPayload(trimmed_payload.c_str());
    TEST_ASSERT_TRUE(result);
    // Note: verifyPayload no longer modifies the input (const reference)
    // Original string remains unchanged
    TEST_ASSERT_EQUAL_STRING("  {\"id\":1}  ", trimmed_payload.c_str());

    Serial.println("✓ Stratum payload verification tests passed");
}

void test_stratum_mining_subscribe_init() {
    Serial.println("\n=== Testing Mining Subscribe Initialization ===");

    mining_subscribe mSub = init_mining_subscribe();

    // Test initialization values
    TEST_ASSERT_EQUAL_STRING("", mSub.extranonce1.c_str());
    TEST_ASSERT_EQUAL_STRING("", mSub.extranonce2.c_str());
    TEST_ASSERT_EQUAL_INT(0, mSub.extranonce2_size);
    TEST_ASSERT_EQUAL_STRING("", mSub.sub_details.c_str());

    // Test structure field sizes
    TEST_ASSERT_EQUAL_size_t(80, sizeof(mSub.wName));
    TEST_ASSERT_EQUAL_size_t(20, sizeof(mSub.wPass));

    Serial.println("✓ Mining subscribe initialization tests passed");
}

void test_stratum_method_parsing() {
    Serial.println("\n=== Testing Stratum Method Parsing ===");

    // Test mining.notify method
    String notify_json = "{\"id\":null,\"method\":\"mining.notify\",\"params\":[]}";
    stratum_method method = parse_mining_method(notify_json.c_str());
    TEST_ASSERT_EQUAL_INT(MINING_NOTIFY, method);

    // Test mining.set_difficulty method
    String difficulty_json = "{\"id\":null,\"method\":\"mining.set_difficulty\",\"params\":[0.5]}";
    method = parse_mining_method(difficulty_json.c_str());
    TEST_ASSERT_EQUAL_INT(MINING_SET_DIFFICULTY, method);

    // Test success response (no method field, error is null)
    String success_json = "{\"id\":1,\"result\":true,\"error\":null}";
    method = parse_mining_method(success_json.c_str());
    TEST_ASSERT_EQUAL_INT(STRATUM_SUCCESS, method);

    // Test unknown method
    String unknown_json = "{\"id\":1,\"method\":\"unknown.method\",\"params\":[]}";
    method = parse_mining_method(unknown_json.c_str());
    TEST_ASSERT_EQUAL_INT(STRATUM_UNKNOWN, method);

    // Test malformed JSON
    String malformed_json = "{\"id\":1,\"method\":\"mining.notify\""; // Missing closing brace
    method = parse_mining_method(malformed_json.c_str());
    TEST_ASSERT_EQUAL_INT(STRATUM_PARSE_ERROR, method);

    // Test empty payload
    String empty_json = "";
    method = parse_mining_method(empty_json.c_str());
    TEST_ASSERT_EQUAL_INT(STRATUM_PARSE_ERROR, method);

    Serial.println("✓ Stratum method parsing tests passed");
}

void test_stratum_check_error() {
    Serial.println("\n=== Testing Stratum Error Handling ===");

    StaticJsonDocument<BUFFER_JSON_DOC> error_doc;
    JsonArray error_array = error_doc.createNestedArray("error");
    error_array.add(25);
    error_array.add("authorization failed");
    TEST_ASSERT_TRUE(checkError(error_doc));

    StaticJsonDocument<BUFFER_JSON_DOC> no_error_doc;
    no_error_doc["result"] = true;
    TEST_ASSERT_FALSE(checkError(no_error_doc));

    StaticJsonDocument<BUFFER_JSON_DOC> empty_error_doc;
    empty_error_doc.createNestedArray("error");
    TEST_ASSERT_FALSE(checkError(empty_error_doc));

    Serial.println("✓ Stratum error handling tests passed");
}

void test_stratum_extract_id() {
    Serial.println("\n=== Testing ID Extraction ===");

    // Test valid ID extraction
    String json_with_id = "{\"id\":12345,\"result\":true}";
    unsigned long extracted_id = parse_extract_id(json_with_id.c_str());
    TEST_ASSERT_EQUAL_UINT32(12345, extracted_id);

    // Test ID extraction with different value
    String json_with_id2 = "{\"error\":null,\"id\":999,\"method\":\"test\"}";
    extracted_id = parse_extract_id(json_with_id2.c_str());
    TEST_ASSERT_EQUAL_UINT32(999, extracted_id);

    // Test missing ID field
    String json_no_id = "{\"result\":true,\"error\":null}";
    extracted_id = parse_extract_id(json_no_id.c_str());
    TEST_ASSERT_EQUAL_UINT32(0, extracted_id);

    // Test malformed JSON
    String malformed_json = "{\"id\":123"; // Missing closing brace
    extracted_id = parse_extract_id(malformed_json.c_str());
    TEST_ASSERT_EQUAL_UINT32(0, extracted_id);

    Serial.println("✓ ID extraction tests passed");
}

void test_stratum_constants() {
    Serial.println("\n=== Testing Stratum Constants ===");

    // Test that stratum constants are defined correctly
    TEST_ASSERT_TRUE(MAX_MERKLE_BRANCHES > 0);
    TEST_ASSERT_TRUE(HASH_SIZE > 0);
    TEST_ASSERT_TRUE(COINBASE_SIZE > 0);
    TEST_ASSERT_TRUE(COINBASE2_SIZE > 0);
    TEST_ASSERT_TRUE(BUFFER_JSON_DOC > 0);
    TEST_ASSERT_TRUE(BUFFER > 0);

    // Print values for verification
    Serial.print("MAX_MERKLE_BRANCHES: "); Serial.println(MAX_MERKLE_BRANCHES);
    Serial.print("HASH_SIZE: "); Serial.println(HASH_SIZE);
    Serial.print("COINBASE_SIZE: "); Serial.println(COINBASE_SIZE);
    Serial.print("COINBASE2_SIZE: "); Serial.println(COINBASE2_SIZE);
    Serial.print("BUFFER_JSON_DOC: "); Serial.println(BUFFER_JSON_DOC);
    Serial.print("BUFFER: "); Serial.println(BUFFER);

    Serial.println("✓ Stratum constants tests passed");
}

void test_stratum_parse_mining_subscribe() {
    Serial.println("\n=== Testing Parse Mining Subscribe ===");

    // Test valid subscribe response
    String valid_response = "{\"id\":1,\"result\":[[[\"mining.set_difficulty\",\"00000001\"],[\"mining.notify\",\"00000002\"]],\"08000002\",4],\"error\":null}";
    mining_subscribe mSub;

    bool result = parse_mining_subscribe(valid_response.c_str(), mSub);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_STRING("00000001", mSub.sub_details.c_str());
    TEST_ASSERT_EQUAL_STRING("08000002", mSub.extranonce1.c_str());
    TEST_ASSERT_EQUAL_INT(4, mSub.extranonce2_size);

    Serial.print("  sub_details: "); Serial.println(mSub.sub_details);
    Serial.print("  extranonce1: "); Serial.println(mSub.extranonce1);
    Serial.print("  extranonce2_size: "); Serial.println(mSub.extranonce2_size);

    // Test with empty extranonce1 (should fail validation)
    String invalid_response = "{\"id\":1,\"result\":[[[\"mining.set_difficulty\",\"00000001\"]],\"\",4],\"error\":null}";
    mining_subscribe mSub2;
    result = parse_mining_subscribe(invalid_response.c_str(), mSub2);
    // Function returns true but caller checks extranonce1.length()
    TEST_ASSERT_EQUAL_INT(0, mSub2.extranonce1.length());

    // Test malformed JSON
    String malformed = "{\"id\":1,\"result\":[";
    mining_subscribe mSub3;
    result = parse_mining_subscribe(malformed.c_str(), mSub3);
    TEST_ASSERT_FALSE(result);

    Serial.println("✓ Parse mining subscribe tests passed");
}

void test_stratum_parse_mining_set_difficulty() {
    Serial.println("\n=== Testing Parse Mining Set Difficulty ===");

    // Test valid difficulty setting
    String valid_diff = "{\"id\":null,\"method\":\"mining.set_difficulty\",\"params\":[0.5]}";
    double difficulty = 0.0;

    bool result = parse_mining_set_difficulty(valid_diff.c_str(), difficulty);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_DOUBLE(0.5, difficulty);
    Serial.print("  Difficulty: "); Serial.println(difficulty, 12);

    // Test with different difficulty
    String valid_diff2 = "{\"id\":null,\"method\":\"mining.set_difficulty\",\"params\":[1024.0]}";
    result = parse_mining_set_difficulty(valid_diff2.c_str(), difficulty);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_DOUBLE(1024.0, difficulty);

    // Test with very small difficulty
    String valid_diff3 = "{\"id\":null,\"method\":\"mining.set_difficulty\",\"params\":[0.00015]}";
    result = parse_mining_set_difficulty(valid_diff3.c_str(), difficulty);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_DOUBLE(0.00015, difficulty);

    // Test malformed JSON
    String malformed = "{\"params\":[";
    result = parse_mining_set_difficulty(malformed.c_str(), difficulty);
    TEST_ASSERT_FALSE(result);

    // Test missing params
    String no_params = "{\"id\":null,\"method\":\"mining.set_difficulty\"}";
    result = parse_mining_set_difficulty(no_params.c_str(), difficulty);
    TEST_ASSERT_FALSE(result);

    Serial.println("✓ Parse mining set difficulty tests passed");
}

void test_stratum_parse_mining_notify() {
    Serial.println("\n=== Testing Parse Mining Notify ===");

    // Test with minimal valid notify (no merkle branches)
    String valid_notify = "{\"id\":null,\"method\":\"mining.notify\",\"params\":["
        "\"job1\","  // job_id
        "\"00000000000000000000000000000000\","  // prev_block_hash (shortened for test)
        "\"coinbase1\","  // coinb1
        "\"coinbase2\","  // coinb2
        "[],"  // merkle_branch (empty array)
        "\"00000001\","  // version
        "\"1d00ffff\","  // nbits
        "\"4c92809d\","  // ntime
        "true"  // clean_jobs
        "]}";

    mining_job mJob;
    mJob.merkle_branch_len = 0;
    bool result = parse_mining_notify(valid_notify.c_str(), mJob);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_STRING("job1", mJob.job_id.c_str());
    TEST_ASSERT_EQUAL_STRING("00000000000000000000000000000000", mJob.prev_block_hash.c_str());
    TEST_ASSERT_EQUAL_STRING("coinbase1", mJob.coinb1.c_str());
    TEST_ASSERT_EQUAL_STRING("coinbase2", mJob.coinb2.c_str());
    TEST_ASSERT_EQUAL_STRING("00000001", mJob.version.c_str());
    TEST_ASSERT_EQUAL_STRING("1d00ffff", mJob.nbits.c_str());
    TEST_ASSERT_EQUAL_STRING("4c92809d", mJob.ntime.c_str());
    TEST_ASSERT_TRUE(mJob.clean_jobs);
    TEST_ASSERT_EQUAL_INT(0, mJob.merkle_branch_len);

    Serial.print("  job_id: "); Serial.println(mJob.job_id);
    Serial.print("  version: "); Serial.println(mJob.version);
    Serial.print("  nbits: "); Serial.println(mJob.nbits);
    Serial.print("  clean_jobs: "); Serial.println(mJob.clean_jobs);

    // Test with merkle branches
    String notify_with_merkle = "{\"id\":null,\"method\":\"mining.notify\",\"params\":["
        "\"job2\","
        "\"0000000000000000\","
        "\"cb1\","
        "\"cb2\","
        "[\"merkle1\",\"merkle2\"],"  // Two merkle hashes
        "\"00000002\","
        "\"1d00ffff\","
        "\"4c92809e\","
        "false"
        "]}";

    mining_job mJob2;
    result = parse_mining_notify(notify_with_merkle.c_str(), mJob2);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL_INT(2, mJob2.merkle_branch_len);
    TEST_ASSERT_FALSE(mJob2.clean_jobs);

    // Test merkle branch exceeding configured limit
    StaticJsonDocument<BUFFER_JSON_DOC> oversized_doc;
    oversized_doc["id"] = nullptr;
    oversized_doc["method"] = "mining.notify";
    JsonArray oversized_params = oversized_doc.createNestedArray("params");
    oversized_params.add("job3");
    oversized_params.add("0000000000000000");
    oversized_params.add("cb1");
    oversized_params.add("cb2");
    JsonArray oversized_merkle = oversized_params.createNestedArray();
    for (int i = 0; i < MAX_MERKLE_BRANCHES + 1; ++i) {
        oversized_merkle.add("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    }
    oversized_params.add("00000002");
    oversized_params.add("1d00ffff");
    oversized_params.add("4c92809f");
    oversized_params.add(true);
    String oversized_payload;
    serializeJson(oversized_doc, oversized_payload);

    mining_job oversized_job;
    result = parse_mining_notify(oversized_payload.c_str(), oversized_job);
    TEST_ASSERT_FALSE(result);

    // Test string length validation on job_id
    String long_job_id;
    for (size_t i = 0; i < MAX_JOB_ID_LEN + 1; ++i) {
        long_job_id += 'a';
    }

    StaticJsonDocument<BUFFER_JSON_DOC> long_doc;
    long_doc["id"] = nullptr;
    long_doc["method"] = "mining.notify";
    JsonArray long_params = long_doc.createNestedArray("params");
    long_params.add(long_job_id);
    long_params.add("0000000000000000");
    long_params.add("cb1");
    long_params.add("cb2");
    long_params.createNestedArray(); // empty merkle branch
    long_params.add("00000002");
    long_params.add("1d00ffff");
    long_params.add("4c92809f");
    long_params.add(false);
    String long_payload;
    serializeJson(long_doc, long_payload);

    mining_job long_job;
    result = parse_mining_notify(long_payload.c_str(), long_job);
    TEST_ASSERT_FALSE(result);

    // Test malformed JSON
    String malformed = "{\"method\":\"mining.notify\",\"params\":[";
    mining_job mJob3;
    result = parse_mining_notify(malformed.c_str(), mJob3);
    TEST_ASSERT_FALSE(result);

    Serial.println("✓ Parse mining notify tests passed");
}

#endif // STRATUM_TEST

// =============================================================================
// EXTENDED UTILS TESTS (only when UTILS_EXTENDED_TEST is defined)
// =============================================================================

#ifdef UTILS_EXTENDED_TEST

void test_utils_swap_endian_words() {
    Serial.println("\n=== Testing Endian Word Swapping ===");

    const char* hex_input = "12345678";  // 4-byte word
    uint8_t output[4];

    swap_endian_words(hex_input, output);

    // Expected: 0x78, 0x56, 0x34, 0x12 (little endian)
    TEST_ASSERT_EQUAL_UINT8(0x78, output[0]);
    TEST_ASSERT_EQUAL_UINT8(0x56, output[1]);
    TEST_ASSERT_EQUAL_UINT8(0x34, output[2]);
    TEST_ASSERT_EQUAL_UINT8(0x12, output[3]);

    Serial.println("✓ Endian word swapping tests passed");
}

void test_utils_reverse_bytes() {
    Serial.println("\n=== Testing Byte Reversal ===");

    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    reverse_bytes(data, 6);

    // Expected: 0x06, 0x05, 0x04, 0x03, 0x02, 0x01
    TEST_ASSERT_EQUAL_UINT8(0x06, data[0]);
    TEST_ASSERT_EQUAL_UINT8(0x05, data[1]);
    TEST_ASSERT_EQUAL_UINT8(0x04, data[2]);
    TEST_ASSERT_EQUAL_UINT8(0x03, data[3]);
    TEST_ASSERT_EQUAL_UINT8(0x02, data[4]);
    TEST_ASSERT_EQUAL_UINT8(0x01, data[5]);

    // Test even length
    uint8_t data2[] = {0xAA, 0xBB};
    reverse_bytes(data2, 2);
    TEST_ASSERT_EQUAL_UINT8(0xBB, data2[0]);
    TEST_ASSERT_EQUAL_UINT8(0xAA, data2[1]);

    Serial.println("✓ Byte reversal tests passed");
}

void test_utils_le256todouble() {
    Serial.println("\n=== Testing LE256 to Double Conversion ===");

    // Create a test 256-bit value (32 bytes)
    uint8_t target[32];
    memset(target, 0, 32);
    target[0] = 0x01; // Least significant byte

    double result = le256todouble(target);

    // Should be a positive number
    TEST_ASSERT_TRUE(result > 0.0);
    TEST_ASSERT_TRUE(result < 2.0); // Should be 1.0 for this input

    // Test with all zeros (should handle gracefully)
    memset(target, 0, 32);
    result = le256todouble(target);
    TEST_ASSERT_EQUAL_FLOAT(0.0, result);

    Serial.print("LE256 conversion result: "); Serial.println(result, 10);
    Serial.println("✓ LE256 to double conversion tests passed");
}

void test_utils_diff_from_target() {
    Serial.println("\n=== Testing Difficulty from Target ===");

    // Create a test target
    uint8_t target[32];
    memset(target, 0, 32);
    target[0] = 0x01; // Small target = high difficulty

    double difficulty = diff_from_target(target);

    // Difficulty should be a large positive number
    TEST_ASSERT_TRUE(difficulty > 0.0);

    // Test with larger target (lower difficulty)
    memset(target, 0xFF, 32);
    double low_difficulty = diff_from_target(target);
    TEST_ASSERT_TRUE(low_difficulty > 0.0);
    TEST_ASSERT_TRUE(low_difficulty < difficulty); // Larger target = lower difficulty

    Serial.print("High difficulty: "); Serial.println(difficulty, 2);
    Serial.print("Low difficulty: "); Serial.println(low_difficulty, 2);
    Serial.println("✓ Difficulty from target tests passed");
}

void test_utils_check_valid_hash() {
    Serial.println("\n=== Testing Hash Validation ===");

    // Test Case 1: Hash clearly meets target (many leading zeros)
    uint8_t valid_hash[32] = {
        0x00, 0x00, 0x00, 0x01, 0x23, 0x45, 0x67, 0x89,
        0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89,
        0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89,
        0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89
    };
    uint8_t easy_target[32] = {
        0x00, 0x00, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };

    bool result = checkValid(valid_hash, easy_target);
    TEST_ASSERT_TRUE(result);
    Serial.println("  Test 1: Valid hash meets target - PASS");

    // Test Case 2: Hash exceeds target (all high values)
    uint8_t invalid_hash[32];
    memset(invalid_hash, 0xff, 32);

    result = checkValid(invalid_hash, easy_target);
    TEST_ASSERT_FALSE(result);
    Serial.println("  Test 2: Invalid hash exceeds target - PASS");

    // Test Case 3: Boundary condition - hash equals target exactly
    uint8_t boundary_hash[32] = {
        0x00, 0x00, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };
    uint8_t boundary_target[32];
    memcpy(boundary_target, boundary_hash, 32);

    result = checkValid(boundary_hash, boundary_target);
    TEST_ASSERT_TRUE(result);
    Serial.println("  Test 3: Hash equals target exactly - PASS");

    // Test Case 4: Hash just barely meets target (one bit difference)
    uint8_t barely_valid[32] = {
        0x00, 0x00, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xfe,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };

    result = checkValid(barely_valid, easy_target);
    TEST_ASSERT_TRUE(result);
    Serial.println("  Test 4: Hash barely meets target - PASS");

    // Test Case 5: Hash just barely exceeds target
    uint8_t barely_invalid[32] = {
        0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    result = checkValid(barely_invalid, easy_target);
    TEST_ASSERT_FALSE(result);
    Serial.println("  Test 5: Hash barely exceeds target - PASS");

    // Test Case 6: Null pointer handling
    result = checkValid(nullptr, easy_target);
    TEST_ASSERT_FALSE(result);
    Serial.println("  Test 6: Null hash pointer handling - PASS");

    result = checkValid(valid_hash, nullptr);
    TEST_ASSERT_FALSE(result);
    Serial.println("  Test 7: Null target pointer handling - PASS");

    // Test Case 7: Real-world difficulty test (Bitcoin difficulty 1)
    // Target for difficulty 1: 0x00000000ffff0000000000000000000000000000000000000000000000000000
    uint8_t diff1_target[32] = {
        0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    // Hash that would meet difficulty 1
    uint8_t diff1_valid_hash[32] = {
        0x00, 0x00, 0x00, 0x00, 0x12, 0x34, 0x56, 0x78,
        0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78,
        0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78,
        0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78
    };

    result = checkValid(diff1_valid_hash, diff1_target);
    TEST_ASSERT_TRUE(result);
    Serial.println("  Test 8: Real-world difficulty 1 valid hash - PASS");

    Serial.println("✓ Hash validation tests passed (all 8 test cases)");
}

void test_utils_swab32() {
    Serial.println("\n=== Testing 32-bit Byte Swap ===");

    uint32_t input = 0x12345678;
    uint32_t result = swab32(input);

    // Expected: 0x78563412
    TEST_ASSERT_EQUAL_UINT32(0x78563412, result);

    // Test with different value
    input = 0xDEADBEEF;
    result = swab32(input);
    TEST_ASSERT_EQUAL_UINT32(0xEFBEADDE, result);

    Serial.printf("swab32(0x12345678) = 0x%08X\n", swab32(0x12345678));
    Serial.println("✓ 32-bit byte swap tests passed");
}

void test_utils_get_random_extranonce2() {
    Serial.println("\n=== Testing Random Extranonce2 Generation ===");

    char extranonce2_size2[5]; // 2 bytes = 4 hex chars + null
    char extranonce2_size4[9]; // 4 bytes = 8 hex chars + null
    char extranonce2_size8[17]; // 8 bytes = 16 hex chars + null

    // Test size 2
    getRandomExtranonce2(2, extranonce2_size2);
    TEST_ASSERT_EQUAL_size_t(4, strlen(extranonce2_size2));
    Serial.print("Extranonce2 (size 2): "); Serial.println(extranonce2_size2);

    // Test size 4
    getRandomExtranonce2(4, extranonce2_size4);
    TEST_ASSERT_EQUAL_size_t(8, strlen(extranonce2_size4));
    Serial.print("Extranonce2 (size 4): "); Serial.println(extranonce2_size4);

    // Test size 8
    getRandomExtranonce2(8, extranonce2_size8);
    TEST_ASSERT_EQUAL_size_t(16, strlen(extranonce2_size8));
    Serial.print("Extranonce2 (size 8): "); Serial.println(extranonce2_size8);

    // Test randomness - generate multiple and check they're different
    char test1[9], test2[9], test3[9];
    getRandomExtranonce2(4, test1);
    getRandomExtranonce2(4, test2);
    getRandomExtranonce2(4, test3);

    // At least two should be different (extremely unlikely all three are the same)
    bool all_same = (strcmp(test1, test2) == 0 && strcmp(test2, test3) == 0);
    TEST_ASSERT_FALSE(all_same);

    Serial.println("✓ Random extranonce2 generation tests passed");
}

void test_utils_get_next_extranonce2() {
    Serial.println("\n=== Testing Sequential Extranonce2 Generation ===");

    // Test size 2 (4 hex chars)
    char extranonce2[9] = "00000000";
    getNextExtranonce2(4, extranonce2);

    Serial.print("Initial: 00000000 -> Next: "); Serial.println(extranonce2);
    TEST_ASSERT_EQUAL_STRING("00000001", extranonce2);

    // Test increment
    getNextExtranonce2(4, extranonce2);
    Serial.print("After increment: "); Serial.println(extranonce2);
    TEST_ASSERT_EQUAL_STRING("00000002", extranonce2);

    // Test with larger value
    strcpy(extranonce2, "000000ff");
    getNextExtranonce2(4, extranonce2);
    Serial.print("From 000000ff: "); Serial.println(extranonce2);
    TEST_ASSERT_EQUAL_STRING("00000100", extranonce2);

    // Test string is null-terminated
    TEST_ASSERT_EQUAL_CHAR('\0', extranonce2[8]);

    Serial.println("✓ Sequential extranonce2 generation tests passed");
}

void test_utils_init_miner_data() {
    Serial.println("\n=== Testing Init Miner Data ===");

    miner_data mMiner = init_miner_data();

    // Verify all arrays are zero-initialized
    bool all_target_zero = true;
    bool all_pooltarget_zero = true;
    bool all_merkle_zero = true;
    bool all_blockheader_zero = true;

    for (size_t i = 0; i < 32; i++) {
        if (mMiner.bytearray_target[i] != 0) all_target_zero = false;
        if (mMiner.bytearray_pooltarget[i] != 0) all_pooltarget_zero = false;
        if (mMiner.merkle_result[i] != 0) all_merkle_zero = false;
    }

    for (size_t i = 0; i < 128; i++) {
        if (mMiner.bytearray_blockheader[i] != 0) all_blockheader_zero = false;
    }

    TEST_ASSERT_TRUE(all_target_zero);
    TEST_ASSERT_TRUE(all_pooltarget_zero);
    TEST_ASSERT_TRUE(all_merkle_zero);
    TEST_ASSERT_TRUE(all_blockheader_zero);

    Serial.println("  bytearray_target: all zeros - PASS");
    Serial.println("  bytearray_pooltarget: all zeros - PASS");
    Serial.println("  merkle_result: all zeros - PASS");
    Serial.println("  bytearray_blockheader: all zeros - PASS");

    // Verify structure sizes
    TEST_ASSERT_EQUAL_size_t(32, sizeof(mMiner.bytearray_target));
    TEST_ASSERT_EQUAL_size_t(32, sizeof(mMiner.bytearray_pooltarget));
    TEST_ASSERT_EQUAL_size_t(32, sizeof(mMiner.merkle_result));
    TEST_ASSERT_EQUAL_size_t(128, sizeof(mMiner.bytearray_blockheader));

    Serial.println("  Structure sizes verified - PASS");

    Serial.println("✓ Init miner data tests passed");
}

void test_utils_calculate_mining_data_basic() {
    Serial.println("\n=== Testing Calculate Mining Data ===");

    // Test Case 1: Basic job without merkle branches
    Serial.println("\n  Test 1: Basic job (no merkle branches)");
    mining_subscribe mWorker;
    mWorker.extranonce1 = "00000000";
    mWorker.extranonce2_size = 4;
    strcpy(mWorker.wName, "test_miner");
    strcpy(mWorker.wPass, "x");

    mining_job mJob;
    mJob.job_id = "test_job";
    mJob.prev_block_hash = "0000000000000000000000000000000000000000000000000000000000000000";
    mJob.coinb1 = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff08";
    mJob.coinb2 = "ffffffff0100f2052a01000000434104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac00000000";
    mJob.version = "00000001";
    mJob.nbits = "1d00ffff";
    mJob.ntime = "4c92809d";
    mJob.clean_jobs = true;

    miner_data mMiner = calculateMiningData(mWorker, mJob);

    // Verify basic structure integrity
    TEST_ASSERT_EQUAL_size_t(32, sizeof(mMiner.bytearray_target));
    TEST_ASSERT_EQUAL_size_t(128, sizeof(mMiner.bytearray_blockheader));

    // Verify nbits expansion (1d00ffff should expand to difficulty 1 target)
    // Expected target: 0x00000000ffff0000000000000000000000000000000000000000000000000000
    uint8_t expected_target[32] = {
        0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_target, mMiner.bytearray_target, 32);
    Serial.println("    nbits expansion verified - PASS");

    // Verify block header has version (should be 0x01000000 little-endian)
    TEST_ASSERT_EQUAL_UINT8(0x01, mMiner.bytearray_blockheader[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00, mMiner.bytearray_blockheader[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00, mMiner.bytearray_blockheader[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00, mMiner.bytearray_blockheader[3]);
    Serial.println("    Block header version verified - PASS");

    // Verify merkle result is not all zeros (coinbase was hashed)
    bool has_merkle_data = false;
    for (size_t i = 0; i < 32; i++) {
        if (mMiner.merkle_result[i] != 0) {
            has_merkle_data = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(has_merkle_data);
    Serial.println("    Merkle root calculation verified - PASS");

    // Test Case 2: Job with merkle branches
    Serial.println("\n  Test 2: Job with merkle branches");
    mining_job mJob2;
    mJob2.merkle_branch_len = 0;
    mJob2.job_id = "test_job_2";
    mJob2.prev_block_hash = "0000000000000000000000000000000000000000000000000000000000000001";
    mJob2.coinb1 = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff08";
    mJob2.coinb2 = "ffffffff0100f2052a01000000434104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac00000000";
    mJob2.version = "00000001";
    mJob2.nbits = "1d00ffff";
    mJob2.ntime = "4c92809d";
    mJob2.clean_jobs = true;

    // Add two merkle branches
    mJob2.merkle_branch[0] = "1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef";
    mJob2.merkle_branch[1] = "fedcba0987654321fedcba0987654321fedcba0987654321fedcba0987654321";
    mJob2.merkle_branch_len = 2;

    miner_data mMiner2 = calculateMiningData(mWorker, mJob2);

    // Verify merkle branches were processed
    TEST_ASSERT_EQUAL_INT(2, mJob2.merkle_branch_len);
    Serial.println("    Merkle branches processed - PASS");

    // Merkle result should be different from first test (different merkle branches)
    bool merkle_results_different = false;
    for (size_t i = 0; i < 32; i++) {
        if (mMiner.merkle_result[i] != mMiner2.merkle_result[i]) {
            merkle_results_different = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(merkle_results_different);
    Serial.println("    Merkle root with branches differs - PASS");

    // Test Case 3: Different extranonce2 sizes
    Serial.println("\n  Test 3: Different extranonce2 sizes");

    // Size 2 (4 hex chars)
    mWorker.extranonce2_size = 2;
    miner_data mMiner_size2 = calculateMiningData(mWorker, mJob);
    TEST_ASSERT_TRUE(isSha256Valid(mMiner_size2.merkle_result));
    Serial.println("    extranonce2_size = 2: PASS");

    // Size 8 (16 hex chars)
    mWorker.extranonce2_size = 8;
    miner_data mMiner_size8 = calculateMiningData(mWorker, mJob);
    TEST_ASSERT_TRUE(isSha256Valid(mMiner_size8.merkle_result));
    Serial.println("    extranonce2_size = 8: PASS");

    // Test Case 4: Different nbits values
    Serial.println("\n  Test 4: Different nbits values");

    // Higher difficulty: 1a05db8b
    mJob.nbits = "1a05db8b";
    mWorker.extranonce2_size = 4;
    miner_data mMiner_high_diff = calculateMiningData(mWorker, mJob);

    // Target should be smaller (higher difficulty)
    // Compare first non-zero bytes
    bool higher_difficulty_confirmed = false;
    for (int i = 0; i < 32; i++) {
        if (expected_target[i] != mMiner_high_diff.bytearray_target[i]) {
            if (i < 4) {
                // First 4 bytes should be smaller for higher difficulty
                higher_difficulty_confirmed = true;
            }
            break;
        }
    }
    TEST_ASSERT_TRUE(higher_difficulty_confirmed ||
                     memcmp(expected_target, mMiner_high_diff.bytearray_target, 32) != 0);
    Serial.println("    Different nbits produces different target - PASS");

    Serial.println("\n✓ Calculate mining data tests passed (all 4 test cases)");
}

void test_utils_edge_cases() {
    Serial.println("\n=== Testing Utils Edge Cases ===");

    // Test Case 1: to_byte_array with insufficient buffer
    Serial.println("\n  Test 1: to_byte_array buffer boundaries");
    uint8_t small_buffer[2];
    int result = to_byte_array("deadbeef", 2, small_buffer);
    TEST_ASSERT_EQUAL_INT(2, result); // Should only convert what fits
    TEST_ASSERT_EQUAL_UINT8(0xde, small_buffer[0]);
    TEST_ASSERT_EQUAL_UINT8(0xad, small_buffer[1]);
    Serial.println("    Insufficient buffer handling - PASS");

    // Test Case 2: to_byte_array null pointer handling
    result = to_byte_array(nullptr, 4, small_buffer);
    TEST_ASSERT_EQUAL_INT(0, result);
    Serial.println("    Null input string handling - PASS");

    result = to_byte_array("dead", 4, nullptr);
    TEST_ASSERT_EQUAL_INT(0, result);
    Serial.println("    Null output buffer handling - PASS");

    result = to_byte_array("dead", 0, small_buffer);
    TEST_ASSERT_EQUAL_INT(0, result);
    Serial.println("    Zero buffer size handling - PASS");

    // Test Case 3: to_byte_array with odd-length hex string
    uint8_t odd_buffer[3];
    result = to_byte_array("12345", 3, odd_buffer);
    TEST_ASSERT_EQUAL_INT(3, result);
    TEST_ASSERT_EQUAL_UINT8(0x01, odd_buffer[0]); // "1" becomes 0x01
    TEST_ASSERT_EQUAL_UINT8(0x23, odd_buffer[1]); // "23"
    TEST_ASSERT_EQUAL_UINT8(0x45, odd_buffer[2]); // "45"
    Serial.println("    Odd-length hex string handling - PASS");

    // Test Case 4: swap_endian_words edge cases
    Serial.println("\n  Test 2: swap_endian_words edge cases");

    // Non-8-aligned input (should return early)
    uint8_t swap_output[4];
    memset(swap_output, 0xAA, sizeof(swap_output));
    swap_endian_words("1234567", swap_output); // 7 chars, not aligned
    // Buffer should remain unchanged
    for (size_t i = 0; i < sizeof(swap_output); ++i) {
        TEST_ASSERT_EQUAL_UINT8(0xAA, swap_output[i]);
    }
    Serial.println("    Non-8-aligned input handling - PASS");

    // Null pointer handling
    swap_endian_words(nullptr, swap_output);
    swap_endian_words("12345678", nullptr);
    Serial.println("    Null pointer handling - PASS");

    // Test Case 5: reverse_bytes edge cases
    Serial.println("\n  Test 3: reverse_bytes edge cases");

    // Size 0 - should not modify
    uint8_t data[4] = {1, 2, 3, 4};
    reverse_bytes(data, 0);
    TEST_ASSERT_EQUAL_UINT8(1, data[0]);
    TEST_ASSERT_EQUAL_UINT8(2, data[1]);
    Serial.println("    Size 0 handling - PASS");

    // Size 1 - should not modify
    reverse_bytes(data, 1);
    TEST_ASSERT_EQUAL_UINT8(1, data[0]);
    Serial.println("    Size 1 handling - PASS");

    // Odd length
    uint8_t odd_data[5] = {1, 2, 3, 4, 5};
    reverse_bytes(odd_data, 5);
    TEST_ASSERT_EQUAL_UINT8(5, odd_data[0]);
    TEST_ASSERT_EQUAL_UINT8(4, odd_data[1]);
    TEST_ASSERT_EQUAL_UINT8(3, odd_data[2]); // Middle stays
    TEST_ASSERT_EQUAL_UINT8(2, odd_data[3]);
    TEST_ASSERT_EQUAL_UINT8(1, odd_data[4]);
    Serial.println("    Odd-length array handling - PASS");

    // Test Case 6: getRandomExtranonce2 invalid sizes
    Serial.println("\n  Test 4: getRandomExtranonce2 edge cases");

    char extranonce[20];
    memset(extranonce, 0xAA, 20);

    // Size 0
    getRandomExtranonce2(0, extranonce);
    TEST_ASSERT_EQUAL_STRING("", extranonce);
    Serial.println("    Size 0 handling - PASS");

    // Size > 8
    memset(extranonce, 0xAA, 20);
    getRandomExtranonce2(9, extranonce);
    TEST_ASSERT_EQUAL_STRING("", extranonce);
    Serial.println("    Size > 8 handling - PASS");

    // Null pointer (should not crash)
    getRandomExtranonce2(4, nullptr);
    Serial.println("    Null pointer handling - PASS");

    // Test Case 7: getNextExtranonce2 edge cases
    Serial.println("\n  Test 5: getNextExtranonce2 edge cases");

    // Null pointer
    getNextExtranonce2(4, nullptr);
    Serial.println("    Null pointer handling - PASS");

    // Invalid sizes
    char test_extra[20] = "12345678";
    getNextExtranonce2(0, test_extra);
    TEST_ASSERT_EQUAL_STRING("12345678", test_extra); // Unchanged
    Serial.println("    Size 0 handling - PASS");

    strcpy(test_extra, "12345678");
    getNextExtranonce2(9, test_extra);
    TEST_ASSERT_EQUAL_STRING("12345678", test_extra); // Unchanged
    Serial.println("    Size > 8 handling - PASS");

    // Test Case 8: le256todouble with extreme values
    Serial.println("\n  Test 6: le256todouble edge cases");

    // All zeros
    uint8_t zero_target[32];
    memset(zero_target, 0, 32);
    double result_double = le256todouble(zero_target);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, result_double);
    Serial.println("    All zeros handling - PASS");

    // All 0xFF (maximum value)
    uint8_t max_target[32];
    memset(max_target, 0xFF, 32);
    result_double = le256todouble(max_target);
    TEST_ASSERT_TRUE(result_double > 0.0);
    Serial.println("    Maximum value handling - PASS");

    Serial.println("\n✓ Edge case tests passed (all 8 test groups)");
}

void test_utils_buffer_overflow_protection() {
    Serial.println("\n=== Testing Buffer Overflow Protection ===");

    // Test Case 1: calculateMiningData with oversized coinbase
    Serial.println("\n  Test 1: Oversized coinbase handling");

    mining_subscribe mWorker;
    mWorker.extranonce1 = "00";
    mWorker.extranonce2_size = 4;
    strcpy(mWorker.wName, "test");
    strcpy(mWorker.wPass, "x");

    mining_job mJob;
    mJob.job_id = "overflow_test";
    mJob.prev_block_hash = "0000000000000000000000000000000000000000000000000000000000000000";

    // Create an extremely long coinb1 that would exceed buffer when combined
    String long_coinb1 = "";
    for (int i = 0; i < 300; i++) {
        long_coinb1 += "aa"; // 600 hex chars = 300 bytes
    }
    mJob.coinb1 = long_coinb1;
    mJob.coinb2 = "ff";
    mJob.version = "00000001";
    mJob.nbits = "1d00ffff";
    mJob.ntime = "4c92809d";
    mJob.clean_jobs = true;

    // Should handle gracefully without crash
    miner_data result = calculateMiningData(mWorker, mJob);

    // Coinbase overflow should leave merkle result untouched (all zeros)
    bool merkle_all_zero = true;
    for (size_t i = 0; i < sizeof(result.merkle_result); ++i) {
        if (result.merkle_result[i] != 0) {
            merkle_all_zero = false;
            break;
        }
    }
    TEST_ASSERT_TRUE(merkle_all_zero);
    Serial.println("    Oversized coinbase handled safely - PASS");

    // Test Case 2: Normal size after overflow test (verify recovery)
    Serial.println("\n  Test 2: Recovery after overflow");

    mJob.coinb1 = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff08";
    mJob.coinb2 = "ffffffff0100f2052a01000000434104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac00000000";

    miner_data result2 = calculateMiningData(mWorker, mJob);

    // Should work normally after overflow condition
    TEST_ASSERT_TRUE(isSha256Valid(result2.merkle_result));
    Serial.println("    Normal operation after overflow - PASS");

    // Test Case 3: Maximum valid merkle branches
    Serial.println("\n  Test 3: Maximum merkle branches");

    // Add many merkle branches (stress test)
    for (int i = 0; i < 10; i++) {
        mJob.merkle_branch[i] = "1111111111111111111111111111111111111111111111111111111111111111";
    }
    mJob.merkle_branch_len = 10;

    miner_data result3 = calculateMiningData(mWorker, mJob);

    TEST_ASSERT_TRUE(isSha256Valid(result3.merkle_result));
    Serial.println("    Multiple merkle branches handled - PASS");

    Serial.println("\n✓ Buffer overflow protection tests passed (all 3 test cases)");
}

void test_utils_mining_workflow_integration() {
    Serial.println("\n=== Testing Complete Mining Workflow Integration ===");

    // Setup complete mining workflow with realistic data
    Serial.println("\n  Setting up mining workflow with realistic pool data");

    mining_subscribe mWorker;
    mWorker.extranonce1 = "f8000000";
    mWorker.extranonce2_size = 4;
    strcpy(mWorker.wName, "nerdminer");
    strcpy(mWorker.wPass, "x");

    mining_job mJob;
    mJob.merkle_branch_len = 0;
    mJob.job_id = "integration_test";
    // Simplified realistic prev_block_hash
    mJob.prev_block_hash = "00000000000000000001b2c5e3a7f9d8e6c4b2a0987654321fedcba098765432";
    mJob.coinb1 = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff20";
    mJob.coinb2 = "0d2f6e6f64655374726174756d2f00000000014a355009000000001976a914";
    mJob.version = "20000000";
    mJob.nbits = "1a05db8b"; // Realistic difficulty
    mJob.ntime = "65432100";
    mJob.clean_jobs = true;

    // Add a single merkle branch
    mJob.merkle_branch[0] = "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890";
    mJob.merkle_branch_len = 1;

    Serial.println("  Step 1: Calculate mining data from stratum job");
    miner_data mMiner = calculateMiningData(mWorker, mJob);

    // Verify mining data structure
    TEST_ASSERT_EQUAL_size_t(32, sizeof(mMiner.bytearray_target));
    TEST_ASSERT_EQUAL_size_t(128, sizeof(mMiner.bytearray_blockheader));
    Serial.println("    Mining data structure verified - PASS");

    // Step 2: Verify target is valid (not all zeros)
    Serial.println("  Step 2: Verify target validity");
    bool target_valid = isSha256Valid(mMiner.bytearray_target);
    TEST_ASSERT_TRUE(target_valid);
    Serial.println("    Target is valid - PASS");

    // Step 3: Verify merkle result is valid
    Serial.println("  Step 3: Verify merkle root calculation");
    bool merkle_valid = isSha256Valid(mMiner.merkle_result);
    TEST_ASSERT_TRUE(merkle_valid);
    Serial.println("    Merkle root is valid - PASS");

    // Step 4: Create a hash that meets the target (simulated valid solution)
    Serial.println("  Step 4: Test hash validation against target");

    // Create a hash with many leading zeros (would meet low difficulty)
    uint8_t test_hash_valid[32] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x23, 0x45,
        0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45,
        0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45,
        0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45
    };

    bool is_valid_share = checkValid(test_hash_valid, mMiner.bytearray_target);
    TEST_ASSERT_TRUE(is_valid_share);
    Serial.println("    Valid hash meets target - PASS");

    // Step 5: Test invalid hash (doesn't meet target)
    Serial.println("  Step 5: Test invalid hash rejection");

    uint8_t test_hash_invalid[32];
    memset(test_hash_invalid, 0xFF, 32); // All high values

    bool is_invalid_share = checkValid(test_hash_invalid, mMiner.bytearray_target);
    TEST_ASSERT_FALSE(is_invalid_share);
    Serial.println("    Invalid hash rejected - PASS");

    // Step 6: Verify block header structure integrity
    Serial.println("  Step 6: Verify block header structure");

    // Check version field (first 4 bytes, little-endian)
    TEST_ASSERT_EQUAL_UINT8(0x00, mMiner.bytearray_blockheader[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00, mMiner.bytearray_blockheader[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00, mMiner.bytearray_blockheader[2]);
    TEST_ASSERT_EQUAL_UINT8(0x20, mMiner.bytearray_blockheader[3]);
    Serial.println("    Block header version correct - PASS");

    // Verify nonce field exists at position 76-79 (should be 0x00000000 initially)
    TEST_ASSERT_EQUAL_UINT8(0x00, mMiner.bytearray_blockheader[76]);
    TEST_ASSERT_EQUAL_UINT8(0x00, mMiner.bytearray_blockheader[77]);
    TEST_ASSERT_EQUAL_UINT8(0x00, mMiner.bytearray_blockheader[78]);
    TEST_ASSERT_EQUAL_UINT8(0x00, mMiner.bytearray_blockheader[79]);
    Serial.println("    Block header nonce field present - PASS");

    // Step 7: Test workflow with different difficulty
    Serial.println("  Step 7: Test workflow with different difficulty");

    mJob.nbits = "1d00ffff"; // Difficulty 1 (easier target)
    miner_data mMiner_easy = calculateMiningData(mWorker, mJob);

    // Easy target should be larger than hard target (numerically)
    bool targets_different = false;
    for (int i = 0; i < 32; i++) {
        if (mMiner.bytearray_target[i] != mMiner_easy.bytearray_target[i]) {
            targets_different = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(targets_different);
    Serial.println("    Different difficulties produce different targets - PASS");

    // The same hash might meet easier target
    bool meets_easy_target = checkValid(test_hash_valid, mMiner_easy.bytearray_target);
    TEST_ASSERT_TRUE(meets_easy_target);
    Serial.println("    Valid hash meets easier target - PASS");

    // Step 8: Complete workflow validation
    Serial.println("  Step 8: Complete workflow validation");

    // Full workflow: Job received → Data calculated → Target expanded → Hash validated
    TEST_ASSERT_TRUE(isSha256Valid(mMiner_easy.bytearray_target));
    TEST_ASSERT_TRUE(isSha256Valid(mMiner_easy.merkle_result));
    TEST_ASSERT_EQUAL_size_t(128, sizeof(mMiner_easy.bytearray_blockheader));

    Serial.println("    Complete workflow integrity verified - PASS");

    Serial.println("\n✓ Mining workflow integration tests passed (all 8 steps)");
}

#endif // UTILS_EXTENDED_TEST

// =============================================================================
// SHA256 HARDWARE TESTS (only when SHA256_TEST is defined)
// =============================================================================

#ifdef SHA256_TEST

void test_bitcoin_sha256() {
    Serial.println("\n=== Testing Bitcoin SHA256 ===");

    // Test vector: empty string
    const char* input = "";
    uint8_t expected_hash[32] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
        0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
        0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
        0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
    };

    uint8_t result[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, (const unsigned char*)input, strlen(input));
    mbedtls_sha256_finish(&ctx, result);
    mbedtls_sha256_free(&ctx);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_hash, result, 32);

    Serial.print("Expected: ");
    for (int i = 0; i < 32; i++) {
        Serial.printf("%02x", expected_hash[i]);
    }
    Serial.println();
    Serial.print("Got:      ");
    for (int i = 0; i < 32; i++) {
        Serial.printf("%02x", result[i]);
    }
    Serial.println();
}

void test_bitcoin_double_sha256() {
    Serial.println("\n=== Testing Bitcoin Double SHA256 ===");

    // Test vector: "hello"
    const char* input = "hello";
    uint8_t expected_hash[32] = {
        0x95, 0x95, 0xc9, 0xdf, 0x90, 0x07, 0x51, 0x48,
        0xeb, 0x06, 0x86, 0x03, 0x65, 0xdf, 0x33, 0x58,
        0x4b, 0x75, 0xbf, 0xf7, 0x82, 0xa5, 0x10, 0xc6,
        0xcd, 0x48, 0x83, 0xa4, 0x19, 0x83, 0x3d, 0x50
    };

    uint8_t first_hash[32];
    uint8_t result[32];

    // First SHA256
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, (const unsigned char*)input, strlen(input));
    mbedtls_sha256_finish(&ctx, first_hash);
    mbedtls_sha256_free(&ctx);

    // Second SHA256
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, first_hash, 32);
    mbedtls_sha256_finish(&ctx, result);
    mbedtls_sha256_free(&ctx);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_hash, result, 32);

    Serial.print("Expected: ");
    for (int i = 0; i < 32; i++) {
        Serial.printf("%02x", expected_hash[i]);
    }
    Serial.println();
    Serial.print("Got:      ");
    for (int i = 0; i < 32; i++) {
        Serial.printf("%02x", result[i]);
    }
    Serial.println();
}

void test_hardware_sha256_performance() {
    Serial.println("\n=== Testing SHA256 Hardware Performance ===");

    const int iterations = 1000;
    uint8_t test_data[64];
    uint8_t result[32];

    // Fill test data
    for (int i = 0; i < 64; i++) {
        test_data[i] = (uint8_t)(i * 37 + 42); // Some pseudo-random data
    }

    unsigned long start_time = micros();

    // Test hardware SHA256
    for (int i = 0; i < iterations; i++) {
        mbedtls_sha256_context ctx;
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts(&ctx, 0);
        mbedtls_sha256_update(&ctx, test_data, 64);
        mbedtls_sha256_finish(&ctx, result);
        mbedtls_sha256_free(&ctx);
    }

    unsigned long end_time = micros();
    unsigned long duration = end_time - start_time;

    Serial.print("Hardware SHA256 performance: ");
    Serial.print(iterations);
    Serial.print(" iterations in ");
    Serial.print(duration);
    Serial.print(" microseconds (");
    Serial.print((float)duration / iterations);
    Serial.println(" μs per hash)");

    // Test should complete reasonably fast
    TEST_ASSERT_TRUE(duration < 500000); // < 500ms for 1000 hashes

    Serial.print("Hash rate: ");
    Serial.print((1000000.0 * iterations) / duration);
    Serial.println(" hashes per second");
}

void test_block_header_hashing() {
    Serial.println("\n=== Testing Bitcoin Block Header Hashing ===");

    // Test with a known block header (fake but realistic format)
    uint8_t block_header[80] = {
        // Version (4 bytes, little endian)
        0x01, 0x00, 0x00, 0x00,
        // Previous block hash (32 bytes)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // Merkle root (32 bytes)
        0x3b, 0xa3, 0xed, 0xfd, 0x7a, 0x7b, 0x12, 0xb2, 0x7a, 0xc7, 0x2c, 0x3e,
        0x67, 0x76, 0x8f, 0x61, 0x7f, 0xc8, 0x1b, 0xc3, 0x88, 0x8a, 0x51, 0x32,
        0x3a, 0x9f, 0xb8, 0xaa, 0x4b, 0x1e, 0x5e, 0x4a,
        // Timestamp (4 bytes)
        0x29, 0xab, 0x5f, 0x49,
        // Bits/Difficulty (4 bytes)
        0xff, 0xff, 0x00, 0x1d,
        // Nonce (4 bytes)
        0xf4, 0x9c, 0x44, 0x70
    };

    uint8_t first_hash[32];
    uint8_t final_hash[32];

    // Bitcoin uses double SHA256
    mbedtls_sha256_context ctx;

    // First SHA256
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, block_header, 80);
    mbedtls_sha256_finish(&ctx, first_hash);
    mbedtls_sha256_free(&ctx);

    // Second SHA256
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, first_hash, 32);
    mbedtls_sha256_finish(&ctx, final_hash);
    mbedtls_sha256_free(&ctx);

    TEST_ASSERT_TRUE(true); // Test completion without crashing

    Serial.print("Block header hash: ");
    for (int i = 0; i < 32; i++) {
        Serial.printf("%02x", final_hash[i]);
    }
    Serial.println();
}

void test_mining_hash_validation() {
    Serial.println("\n=== Testing Mining Hash Validation ===");

    // Create a hash with leading zeros (like a valid mining result)
    uint8_t valid_mining_hash[32] = {
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    // Create an invalid hash (no leading zeros)
    uint8_t invalid_mining_hash[32] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };

    // Create a target (difficulty)
    uint8_t target[32] = {
        0x00, 0x00, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };

    bool valid_result = checkValid(valid_mining_hash, target);
    bool invalid_result = checkValid(invalid_mining_hash, target);

    TEST_ASSERT_TRUE(valid_result);
    TEST_ASSERT_FALSE(invalid_result);

    Serial.print("Valid hash check: ");
    Serial.println(valid_result ? "PASS" : "FAIL");
    Serial.print("Invalid hash check: ");
    Serial.println(invalid_result ? "FAIL (expected)" : "PASS (unexpected)");
}

#endif // SHA256_TEST

// =============================================================================
// SIMPLE TEST MODE SPECIFIC TESTS (when SIMPLE_TEST is defined)
// =============================================================================

#ifdef SIMPLE_TEST

void test_simple_framework_assert() {
    // Test our simple assertion framework
    bool test_passed = true;
    TEST_ASSERT_TRUE(test_passed);
    Serial.println("✓ Simple test framework validation passed");
}

void test_memory_info() {
    Serial.println("\n=== Memory Information ===");
    Serial.print("Free heap: ");
    Serial.println(ESP.getFreeHeap());
    Serial.print("Heap size: ");
    Serial.println(ESP.getHeapSize());
    Serial.print("Free PSRAM: ");
    Serial.println(ESP.getFreePsram());

    TEST_ASSERT_TRUE(ESP.getFreeHeap() > 10000); // At least 10KB free
    Serial.println("✓ Memory information tests passed");
}

#endif // SIMPLE_TEST

void setup() {
    Serial.begin(115200);
    delay(2000); // Wait for serial to stabilize

    // Print test mode information
    Serial.println("=== NerdMiner Unified Test Suite ===");
    Serial.println("Running on ESP32-2432S028R");

    #ifdef SIMPLE_TEST
    Serial.println("Test Mode: SIMPLE TEST");
    #elif defined(SHA256_TEST)
    Serial.println("Test Mode: SHA256 HARDWARE TEST");
    #ifdef HARDWARE_SHA256
    Serial.println("Hardware SHA256 acceleration: ENABLED");
    #else
    Serial.println("Hardware SHA256 acceleration: DISABLED (Software fallback)");
    #endif
    #elif defined(MINING_TEST)
    Serial.println("Test Mode: MINING TEST");
    #elif defined(STRATUM_TEST)
    Serial.println("Test Mode: STRATUM TEST");
    #elif defined(UTILS_EXTENDED_TEST)
    Serial.println("Test Mode: UTILS EXTENDED TEST");
    #else
    Serial.println("Test Mode: UNITY EMBEDDED TEST (default)");
    #endif

    Serial.print("Free heap: ");
    Serial.println(ESP.getFreeHeap());
    Serial.print("Chip model: ");
    Serial.println(ESP.getChipModel());
    Serial.print("Chip revision: ");
    Serial.println(ESP.getChipRevision());
    Serial.print("CPU frequency: ");
    Serial.print(ESP.getCpuFreqMHz());
    Serial.println(" MHz");

    UNITY_BEGIN();

    // Run common tests for all modes
    RUN_TEST(test_hex_conversion);
    RUN_TEST(test_to_byte_array);
    RUN_TEST(test_crc32_functions);
    RUN_TEST(test_suffix_string);
    RUN_TEST(test_sha256_validation_basic);

    // Run mode-specific tests
    #ifdef SHA256_TEST
    RUN_TEST(test_bitcoin_sha256);
    RUN_TEST(test_bitcoin_double_sha256);
    RUN_TEST(test_hardware_sha256_performance);
    RUN_TEST(test_block_header_hashing);
    RUN_TEST(test_mining_hash_validation);
    #endif

    #ifdef SIMPLE_TEST
    RUN_TEST(test_simple_framework_assert);
    RUN_TEST(test_memory_info);
    #endif

    #ifdef MINING_TEST
    RUN_TEST(test_mining_nonce_configuration);
    RUN_TEST(test_mining_calculate_mining_data_core_fields);
    RUN_TEST(test_miner_data_structure);
    #endif

    #ifdef STRATUM_TEST
    RUN_TEST(test_stratum_get_next_id);
    RUN_TEST(test_stratum_verify_payload);
    RUN_TEST(test_stratum_mining_subscribe_init);
    RUN_TEST(test_stratum_method_parsing);
    RUN_TEST(test_stratum_check_error);
    RUN_TEST(test_stratum_extract_id);
    RUN_TEST(test_stratum_constants);
    RUN_TEST(test_stratum_parse_mining_subscribe);
    RUN_TEST(test_stratum_parse_mining_set_difficulty);
    RUN_TEST(test_stratum_parse_mining_notify);
    #endif

    #ifdef UTILS_EXTENDED_TEST
    RUN_TEST(test_utils_swap_endian_words);
    RUN_TEST(test_utils_reverse_bytes);
    RUN_TEST(test_utils_le256todouble);
    RUN_TEST(test_utils_diff_from_target);
    RUN_TEST(test_utils_check_valid_hash);
    RUN_TEST(test_utils_swab32);
    RUN_TEST(test_utils_get_random_extranonce2);
    RUN_TEST(test_utils_get_next_extranonce2);
    RUN_TEST(test_utils_init_miner_data);
    RUN_TEST(test_utils_calculate_mining_data_basic);
    RUN_TEST(test_utils_edge_cases);
    RUN_TEST(test_utils_buffer_overflow_protection);
    RUN_TEST(test_utils_mining_workflow_integration);
    #endif

    UNITY_END();

    Serial.println("=== Test Suite Complete ===");
}

void loop() {
    // Test results are shown once in setup()
    delay(5000);
    Serial.println("Tests completed. Reset to run again.");
}
