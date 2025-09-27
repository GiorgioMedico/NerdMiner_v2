#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <unity.h>
#include <stdint.h>
#include <string.h>

// Common test utilities for both native and embedded tests

// Hex string conversion utilities
void hex_string_to_bytes(const char* hex_string, uint8_t* bytes, size_t bytes_len);
void bytes_to_hex_string(const uint8_t* bytes, size_t bytes_len, char* hex_string);

// Memory comparison utilities
void print_bytes_hex(const uint8_t* bytes, size_t len, const char* label);
void assert_bytes_equal(const uint8_t* expected, const uint8_t* actual, size_t len, const char* message);

// Test data validation
bool validate_sha256_hash(const uint8_t* hash);
bool validate_bitcoin_difficulty_target(const uint8_t* target);

// Stratum protocol validation functions
bool validate_json_string(const char* json_str);
bool is_valid_hex_string(const char* hex_str);
bool is_valid_stratum_method(const char* method);

#endif // TEST_UTILS_H