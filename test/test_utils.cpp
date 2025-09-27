#include "test_utils.h"
#include <stdio.h>
#include <ctype.h>

// Convert hex string to byte array
void hex_string_to_bytes(const char* hex_string, uint8_t* bytes, size_t bytes_len) {
    size_t hex_len = strlen(hex_string);
    size_t expected_hex_len = bytes_len * 2;

    TEST_ASSERT_EQUAL_MESSAGE(expected_hex_len, hex_len, "Hex string length mismatch");

    for (size_t i = 0; i < bytes_len; i++) {
        char byte_str[3] = {hex_string[i*2], hex_string[i*2+1], '\0'};
        bytes[i] = (uint8_t)strtoul(byte_str, NULL, 16);
    }
}

// Convert byte array to hex string
void bytes_to_hex_string(const uint8_t* bytes, size_t bytes_len, char* hex_string) {
    for (size_t i = 0; i < bytes_len; i++) {
        sprintf(hex_string + (i * 2), "%02x", bytes[i]);
    }
    hex_string[bytes_len * 2] = '\0';
}

// Print bytes as hex for debugging
void print_bytes_hex(const uint8_t* bytes, size_t len, const char* label) {
    printf("%s: ", label);
    for (size_t i = 0; i < len; i++) {
        printf("%02x", bytes[i]);
    }
    printf("\n");
}

// Assert byte arrays are equal with helpful error message
void assert_bytes_equal(const uint8_t* expected, const uint8_t* actual, size_t len, const char* message) {
    for (size_t i = 0; i < len; i++) {
        if (expected[i] != actual[i]) {
            char error_msg[256];
            sprintf(error_msg, "%s - Bytes differ at position %zu: expected 0x%02x, got 0x%02x",
                    message, i, expected[i], actual[i]);
            TEST_FAIL_MESSAGE(error_msg);
        }
    }
}

// Validate SHA256 hash (32 bytes, not all zeros)
bool validate_sha256_hash(const uint8_t* hash) {
    if (hash == NULL) return false;

    // Check if all bytes are zero (invalid hash)
    for (int i = 0; i < 32; i++) {
        if (hash[i] != 0) return true;
    }
    return false; // All zeros is not a valid hash
}

// Validate Bitcoin difficulty target (32 bytes, proper format)
bool validate_bitcoin_difficulty_target(const uint8_t* target) {
    if (target == NULL) return false;

    // Bitcoin targets should be less than max target
    // Max target: 0x00000000FFFF0000000000000000000000000000000000000000000000000000
    if (target[0] != 0x00 || target[1] != 0x00 || target[2] != 0x00 || target[3] != 0x00) {
        return false; // First 4 bytes should be zero for reasonable difficulties
    }

    return true;
}

// Validate JSON string (basic validation)
bool validate_json_string(const char* json_str) {
    if (json_str == NULL) return false;
    if (strlen(json_str) == 0) return false;

    // Basic check: should start with { and end with }
    size_t len = strlen(json_str);
    if (json_str[0] != '{' || json_str[len - 1] != '}') {
        return false;
    }

    return true;
}

// Validate hex string (contains only hex characters)
bool is_valid_hex_string(const char* hex_str) {
    if (hex_str == NULL) return false;

    size_t len = strlen(hex_str);
    if (len == 0) return false;
    if (len % 2 != 0) return false; // Should be even length

    for (size_t i = 0; i < len; i++) {
        char c = hex_str[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }

    return true;
}

// Validate Stratum method name
bool is_valid_stratum_method(const char* method) {
    if (method == NULL) return false;

    // Check against known Stratum methods
    if (strcmp(method, "mining.subscribe") == 0) return true;
    if (strcmp(method, "mining.authorize") == 0) return true;
    if (strcmp(method, "mining.notify") == 0) return true;
    if (strcmp(method, "mining.submit") == 0) return true;
    if (strcmp(method, "mining.set_difficulty") == 0) return true;
    if (strcmp(method, "mining.set_extranonce") == 0) return true;

    return false;
}