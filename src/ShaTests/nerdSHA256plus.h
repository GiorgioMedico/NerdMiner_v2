/************************************************************************************
*   written by: Bitmaker
*   based on: Blockstream Jade shaLib
*   thanks to @LarryBitcoin

*   Description:

*   NerdSha256plus is a custom C implementation of sha256d based on Blockstream Jade 
    code https://github.com/Blockstream/Jade

    The folowing file can be used on any ESP32 implementation using both cores

*************************************************************************************/
#ifndef nerdSHA256plus_H_
#define nerdSHA256plus_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


struct nerdSHA256_context {
    uint8_t buffer[64];
    uint32_t digest[8];
};

/* Calculate midstate */
IRAM_ATTR void nerd_mids(uint32_t* __restrict digest, const uint8_t* __restrict dataIn);

IRAM_ATTR bool nerd_sha256d(nerdSHA256_context* __restrict midstate, const uint8_t* __restrict dataIn, uint8_t* __restrict doubleHash);

IRAM_ATTR void nerd_sha256_bake(const uint32_t* __restrict digest, const uint8_t* __restrict dataIn, uint32_t* __restrict bake);  //15 words
IRAM_ATTR bool nerd_sha256d_baked_nonce(const uint32_t* __restrict digest, const uint32_t* __restrict bake, uint32_t nonce_be, uint8_t* __restrict doubleHash);
IRAM_ATTR bool nerd_sha256d_baked(const uint32_t* __restrict digest, const uint8_t* __restrict dataIn, const uint32_t* __restrict bake, uint8_t* __restrict doubleHash);

void ByteReverseWords(uint32_t* __restrict out, const uint32_t* __restrict in, uint32_t byteCount);

#endif /* nerdSHA256plus_H_ */
