#ifndef UTILS_API_H
#define UTILS_API_H

#include <stddef.h>
#include <stdint.h>
#include "mining.h"
#include "stratum.h"

/*
 * General byte order swapping functions.
 */
#define	bswap16(x)	__bswap16(x)
#define	bswap32(x)	__bswap32(x)
#define	bswap64(x)	__bswap64(x)



uint8_t hex(char ch);
uint32_t swab32(uint32_t v);

int to_byte_array(const char *in, size_t out_size, uint8_t *out);
void swap_endian_words(const char* hex_words, uint8_t* output);
void reverse_bytes(uint8_t* data, size_t len);

double le256todouble(const void *target);
double diff_from_target(void *target);
bool isSha256Valid(const void* sha256);
bool difficulty_to_target(double difficulty, uint8_t* target_le);

void getRandomExtranonce2(int extranonce2_size, char *extranonce2);
void getNextExtranonce2(int extranonce2_size, char *extranonce2);

miner_data init_miner_data(void);
miner_data calculateMiningData(mining_subscribe& mWorker, mining_job mJob);
bool checkValid(const uint8_t* hash, const uint8_t* target);
void suffix_string(double val, char *buf, size_t bufsiz, int sigdigits);

uint32_t crc32_reset();
uint32_t crc32_add(uint32_t crc32, const void* data, size_t size);
uint32_t crc32_finish(uint32_t crc32);



#endif // UTILS_API_H
