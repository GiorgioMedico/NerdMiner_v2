#ifndef SHA256_TEST_VECTORS_H
#define SHA256_TEST_VECTORS_H

#include <stdint.h>

// SHA256 Test Vectors from NIST and Bitcoin examples

// Test Vector 1: Empty string
static const char* SHA256_TV1_INPUT = "";
static const char* SHA256_TV1_EXPECTED = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

// Test Vector 2: "abc"
static const char* SHA256_TV2_INPUT = "abc";
static const char* SHA256_TV2_EXPECTED = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

// Test Vector 3: "message digest"
static const char* SHA256_TV3_INPUT = "message digest";
static const char* SHA256_TV3_EXPECTED = "f7846f55cf23e14eebeab5b4e1550cad5b509e3348fbc4efa3a1413d393cb650";

// Test Vector 4: 448-bit message
static const char* SHA256_TV4_INPUT = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
static const char* SHA256_TV4_EXPECTED = "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1";

// Bitcoin-specific test vectors

// Bitcoin Block Header Test Vector (simplified)
// This represents the first 80 bytes of a Bitcoin block header
static const uint8_t BITCOIN_BLOCK_HEADER_TV1[80] = {
    // Version (4 bytes): 0x01000000 (little-endian)
    0x01, 0x00, 0x00, 0x00,
    // Previous block hash (32 bytes): Genesis block (all zeros)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Merkle root (32 bytes): Example merkle root
    0x3b, 0xa3, 0xed, 0xfd, 0x7a, 0x7b, 0x12, 0xb2, 0x7a, 0xc7, 0x2c, 0x3e, 0x67, 0x76, 0x8f, 0x61,
    0x7f, 0xc8, 0x1b, 0xc3, 0x88, 0x8a, 0x51, 0x32, 0x3a, 0x9f, 0xb8, 0xaa, 0x4b, 0x1e, 0x5e, 0x4a,
    // Timestamp (4 bytes): 1231006505 (little-endian)
    0x29, 0xab, 0x5f, 0x49,
    // Bits (4 bytes): 0x1d00ffff (little-endian)
    0xff, 0xff, 0x00, 0x1d,
    // Nonce (4 bytes): 2083236893 (little-endian)
    0x1d, 0xac, 0x2b, 0x7c
};

// Expected double SHA256 hash for the above block header
static const char* BITCOIN_BLOCK_HEADER_TV1_EXPECTED = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f";

// SHA256 Double Hash Test Vector
// Input: "hello"
static const char* SHA256_DOUBLE_TV1_INPUT = "hello";
// First SHA256: 2cf24dba4f21d4288dce2c3f9da68a93150f56c5bc3e4dc3ac0b3e6a49a2c0d4
// Second SHA256: 9595c9df90075148eb06860365df33584b75bff782a510c6cd4883a419833d50
static const char* SHA256_DOUBLE_TV1_EXPECTED = "9595c9df90075148eb06860365df33584b75bff782a510c6cd4883a419833d50";

// Bitcoin mining specific constants for testing
#define BITCOIN_BLOCK_HEADER_SIZE 80
#define SHA256_HASH_SIZE 32
#define SHA256_BLOCK_SIZE 64

// Difficulty target test vector (high difficulty for testing)
static const uint8_t DIFFICULTY_TARGET_TV1[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xff, 0xff,  // Very high difficulty target
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Lower difficulty target for easier testing
static const uint8_t DIFFICULTY_TARGET_TV2[32] = {
    0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00,  // Lower difficulty target
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Function to validate test vectors at runtime
void validate_test_vectors(void);

#endif // SHA256_TEST_VECTORS_H