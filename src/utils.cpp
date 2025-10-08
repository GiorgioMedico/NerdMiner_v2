
#include <Arduino.h>
#include "utils.h"
#include "mining.h"
#include "stratum.h"
#include "mbedtls/sha256.h"
#include "esp_random.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "logging.h"

#if __has_include("esp_rom_crc.h")
#include "esp_rom_crc.h"
#elif __has_include("esp32/rom/crc.h")
#include "esp32/rom/crc.h"
#elif __has_include("esp32s2/rom/crc.h")
#include "esp32s2/rom/crc.h"
#elif __has_include("esp32s3/rom/crc.h")
#include "esp32s3/rom/crc.h"
#elif __has_include("esp32c3/rom/crc.h")
#include "esp32c3/rom/crc.h"
#elif __has_include("esp32c6/rom/crc.h")
#include "esp32c6/rom/crc.h"
#elif __has_include("esp32h2/rom/crc.h")
#include "esp32h2/rom/crc.h"
#else
#define NO_ROM_CRC32
#endif

#ifndef bswap_16
#define bswap_16(a) ((((uint16_t) (a) << 8) & 0xff00) | (((uint16_t) (a) >> 8) & 0xff))
#endif

#ifndef bswap_32
#define bswap_32(a) ((((uint32_t) (a) << 24) & 0xff000000) | \
		     (((uint32_t) (a) << 8) & 0xff0000) | \
     		     (((uint32_t) (a) >> 8) & 0xff00) | \
     		     (((uint32_t) (a) >> 24) & 0xff))
#endif

uint32_t swab32(uint32_t v) {
    return bswap_32(v);
}

uint8_t hex(char ch) {
    if (ch >= '0' && ch <= '9')
        return static_cast<uint8_t>(ch - '0');
    if (ch >= 'a' && ch <= 'f')
        return static_cast<uint8_t>(ch - 'a' + 10);
    if (ch >= 'A' && ch <= 'F')
        return static_cast<uint8_t>(ch - 'A' + 10);
    return 0;
}

int to_byte_array(const char *in, size_t out_size, uint8_t *out) 
{
    if (!in || !out || !out_size)
        return 0;

    size_t len = strlen(in);
    size_t count = 0;
    size_t idx = 0;

    if (len % 2 != 0) 
    {
        if (count >= out_size)
            return static_cast<int>(count);
        out[count++] = hex(in[idx++]);
        if (idx >= len)
            return static_cast<int>(count);
    }

    for (; idx + 1 < len && count < out_size; idx += 2) 
    {
        out[count++] = (hex(in[idx]) << 4) | hex(in[idx + 1]);
    }

    return static_cast<int>(count);
}

void swap_endian_words(const char * hex_words, uint8_t * output) 
{
    if (!hex_words || !output)
        return;

    size_t hex_length = strlen(hex_words);
    if (hex_length % 8 != 0)
        return;
    size_t binary_length = hex_length / 2;
    // Decode hex into output
    if (to_byte_array(hex_words, binary_length, output) != (int)binary_length)
        return;
    // Swap each 4-byte word in place
    for (size_t i = 0; i < binary_length; i += 4) 
    {
        uint8_t a = output[i], b = output[i+1], c = output[i+2], d = output[i+3];
        output[i] = d; output[i+1] = c; output[i+2] = b; output[i+3] = a;
    }
}

void reverse_bytes(uint8_t * data, size_t len) 
{
    for (size_t i = 0, j = (len ? len - 1 : 0); i < j; ++i, --j) 
    {
        uint8_t temp = data[i];
        data[i] = data[j];
        data[j] = temp;
    }
}

static const double truediffone = 26959535291011309493156476344723991336010898738574164086137773096960.0;
/* Converts a little endian 256 bit value to a double */
double le256todouble(const void *target) 
{

    const uint8_t *data = static_cast<const uint8_t*>(target);
    double dcut64 = 0.0;
    uint64_t limb = 0;

    memcpy(&limb, data + 24, sizeof(limb));
    dcut64 = static_cast<double>(limb) * 6277101735386680763835789423207666416102355444464034512896.0;

    memcpy(&limb, data + 16, sizeof(limb));
    dcut64 += static_cast<double>(limb) * 340282366920938463463374607431768211456.0;

    memcpy(&limb, data + 8, sizeof(limb));
    dcut64 += static_cast<double>(limb) * 18446744073709551616.0;

    memcpy(&limb, data, sizeof(limb));
    dcut64 += static_cast<double>(limb);

  return dcut64;
}

double diff_from_target(void *target)
{
	double d64, dcut64;

	d64 = truediffone;
	dcut64 = le256todouble(target);
	if (dcut64 == 0.0)
		dcut64 = 1;
	return d64 / dcut64;
}

bool isSha256Valid(const void* sha256)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(sha256);
    for (size_t i = 0; i < 32; ++i)
    {
        if (bytes[i] != 0)
            return true;
    }
    return false;
}

/****************** PREMINING CALCULATIONS ********************/

/**
 * Check if a hash meets the target difficulty
 *
 * Compares the hash against the target in little-endian byte order.
 * A hash is valid if it is numerically less than or equal to the target.
 *
 * @param hash 32-byte SHA256 hash (little-endian)
 * @param target 32-byte difficulty target (big-endian, will be reversed for comparison)
 * @return true if hash meets difficulty target, false otherwise
 */
bool checkValid(const uint8_t* hash, const uint8_t* target) 
{
  if (!hash || !target)
    return false;

  uint8_t diff_target[32];
  memcpy(diff_target, target, 32);
  //convert target to little endian for comparison
  reverse_bytes(diff_target, 32);

  for (int i = 31; i >= 0; i--) 
  {
    if (hash[i] > diff_target[i]) 
    {
      return false;
    }
    if (hash[i] < diff_target[i]) 
    {
      break;
    }
  }

  #ifdef DEBUG_MINING
  DEBUG_SERIAL_PRINT("\tvalid : ");
  for (size_t i = 0; i < 32; i++)
      DEBUG_SERIAL_PRINTF("%02x ", hash[i]);
  DEBUG_SERIAL_PRINTLN();
  #endif

  return true;
}


/**
 * get random extranonce2
 * Uses ESP32 hardware random number generator for better randomness
 *
 * @param extranonce2_size Size in bytes (1-8 supported)
 * @param extranonce2 Output buffer (must be at least extranonce2_size*2+1 bytes)
*/
void getRandomExtranonce2(int extranonce2_size, char *extranonce2) 
{
  if (!extranonce2)
    return;

  // Validate input size
  if (extranonce2_size <= 0 || extranonce2_size > 8) 
  {
    extranonce2[0] = '\0';
    return;
  }

  // Use ESP32 hardware RNG to fill the bytes
  uint8_t random_bytes[8];
  for (int i = 0; i < extranonce2_size && i < 8; i++) 
  {
    random_bytes[i] = (uint8_t)(esp_random() & 0xFF);
  }

  // Convert bytes to hex string with leading zeros
  for (int i = 0; i < extranonce2_size; i++) 
  {
    sprintf(extranonce2 + (i * 2), "%02x", random_bytes[i]);
  }

  // Ensure null termination
  extranonce2[extranonce2_size * 2] = '\0';
}

/**
 * get linear extranonce2
*/
void getNextExtranonce2(int extranonce2_size, char *extranonce2) 
{
  if (!extranonce2 || extranonce2_size <= 0 || extranonce2_size > 8)
    return;

  uint64_t extranonce2_number = strtoull(extranonce2, NULL, 16);
  uint64_t mask = (extranonce2_size >= 8)
                    ? UINT64_MAX
                    : ((1ULL << (extranonce2_size * 8)) - 1ULL);

  extranonce2_number = (extranonce2_number + 1ULL) & mask;

  const int width = extranonce2_size * 2;
  snprintf(extranonce2, width + 1, "%0*llx", width,
           static_cast<unsigned long long>(extranonce2_number));
}

miner_data init_miner_data(void)
{

  miner_data newMinerData{};
  return newMinerData;
}

/**
 * Expand Bitcoin nBits compact representation to full 32-byte target
 *
 * The nBits format encodes a 256-bit target as: [exponent:1 byte][mantissa:3 bytes]
 * This function expands it to a big-endian 32-byte target for hash comparison.
 *
 * @param nbits Hex string of nBits value (e.g., "1a05db8b")
 * @param output Buffer to receive 32-byte expanded target (big-endian)
 * @param out_len Size of output buffer (must be >= 32)
 * @return true if expansion successful, false on invalid input
 */
static bool expandTargetFromNBits(const String& nbits, uint8_t *output, size_t out_len)
{
  if (!output || out_len < 32 || nbits.length() < 6)
    return false;

  uint32_t nbits_value = strtoul(nbits.c_str(), NULL, 16);
  uint32_t exponent = (nbits_value >> 24) & 0xFF;
  uint32_t mantissa = nbits_value & 0x00FFFFFF;

  if (mantissa == 0 || exponent == 0 || exponent > 32)
    return false;

  uint8_t little_target[32] = {0};

  if (exponent <= 3) {
    uint32_t shifted = mantissa >> (8 * (3 - exponent));
    little_target[0] = shifted & 0xFF;
    little_target[1] = (shifted >> 8) & 0xFF;
    little_target[2] = (shifted >> 16) & 0xFF;
  } else {
    size_t pos = static_cast<size_t>(exponent - 3);
    if (pos + 3 > sizeof(little_target))
      return false;
    little_target[pos] = mantissa & 0xFF;
    little_target[pos + 1] = (mantissa >> 8) & 0xFF;
    little_target[pos + 2] = (mantissa >> 16) & 0xFF;
  }

  for (size_t i = 0; i < 32; ++i)
    output[i] = little_target[31 - i];

  return true;
}

/**
 * Calculate all mining data from stratum job and subscription
 *
 * This function performs the complete Bitcoin mining header preparation:
 * 1. Expands nBits to target difficulty
 * 2. Constructs coinbase transaction (coinb1 + extranonce1 + extranonce2 + coinb2)
 * 3. Calculates merkle root from coinbase and merkle branches
 * 4. Builds 80-byte block header with proper endianness
 *
 * @param mWorker Mining subscription with extranonce1/2
 * @param mJob Mining job with coinbase, merkle branches, and block data
 * @return miner_data structure with prepared block header and target
 */
miner_data calculateMiningData(mining_subscribe& mWorker, mining_job mJob){

  miner_data mMiner = init_miner_data();

  // calculate target - target = (nbits[2:]+'00'*(int(nbits[:2],16) - 3)).zfill(64)
    if (!expandTargetFromNBits(mJob.nbits, mMiner.bytearray_target, sizeof(mMiner.bytearray_target))) {
        DEBUG_SERIAL_PRINTLN("Failed to expand nbits target");
        return mMiner;
    }
    
    #ifdef DEBUG_MINING_ALL
    DEBUG_SERIAL_PRINT("    target: ");
    for (size_t i = 0; i < sizeof(mMiner.bytearray_target); ++i)
        DEBUG_SERIAL_PRINTF("%02x", mMiner.bytearray_target[i]);
    DEBUG_SERIAL_PRINTLN("");
    #endif

    // get extranonce2 - use hardware RNG for random extranonce2
    char extranonce2_char[2 * mWorker.extranonce2_size + 1];
    getRandomExtranonce2(mWorker.extranonce2_size, extranonce2_char);
    mWorker.extranonce2 = String(extranonce2_char);
    
    //get coinbase - coinbase_hash_bin = hashlib.sha256(hashlib.sha256(binascii.unhexlify(coinbase)).digest()).digest()
    // Use char buffer instead of String concatenation to avoid memory leaks
    static char coinbase_buffer[512]; // Static buffer to avoid repeated allocation
    int _cbw = snprintf(coinbase_buffer, sizeof(coinbase_buffer), "%s%s%s%s",
             mJob.coinb1.c_str(), mWorker.extranonce1.c_str(),
             mWorker.extranonce2.c_str(), mJob.coinb2.c_str());
    if (_cbw < 0 || (size_t)_cbw >= sizeof(coinbase_buffer)) {
        DEBUG_SERIAL_PRINTLN("Coinbase truncated");
        return mMiner;
    }

    size_t str_len = strlen(coinbase_buffer)/2;
    constexpr size_t coinbase_capacity = sizeof(coinbase_buffer) / 2;
    if (str_len > coinbase_capacity)
    {
        DEBUG_SERIAL_PRINTLN("Coinbase buffer too large");
        return mMiner;
    }

    uint8_t coinbase_bytes[coinbase_capacity];

    size_t res = to_byte_array(coinbase_buffer, coinbase_capacity, coinbase_bytes);
    if (res != str_len)
    {
        DEBUG_SERIAL_PRINTLN("Coinbase hex decode failed");
        return mMiner;
    }

    #ifdef DEBUG_MINING_ALL
    DEBUG_SERIAL_PRINT("    extranonce2: "); DEBUG_SERIAL_PRINTLN(mWorker.extranonce2);
    DEBUG_SERIAL_PRINT("    coinbase: "); DEBUG_SERIAL_PRINTLN(coinbase_buffer);
    DEBUG_SERIAL_PRINT("    coinbase bytes - size: "); DEBUG_SERIAL_PRINTLN(res);
    for (size_t i = 0; i < res; i++)
        DEBUG_SERIAL_PRINTF("%02x", coinbase_bytes[i]);
    DEBUG_SERIAL_PRINTLN("---");
    #endif

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
  
    byte interResult[32]; // 256 bit
    byte shaResult[32]; // 256 bit
  
    mbedtls_sha256_starts_ret(&ctx,0);
    mbedtls_sha256_update_ret(&ctx, coinbase_bytes, str_len);
    mbedtls_sha256_finish_ret(&ctx, interResult);

    mbedtls_sha256_starts_ret(&ctx,0);
    mbedtls_sha256_update_ret(&ctx, interResult, 32);
    mbedtls_sha256_finish_ret(&ctx, shaResult);
    mbedtls_sha256_free(&ctx);

    #ifdef DEBUG_MINING_ALL
    DEBUG_SERIAL_PRINT("    coinbase double sha: ");
    for (size_t i = 0; i < 32; i++)
        DEBUG_SERIAL_PRINTF("%02x", shaResult[i]);
    DEBUG_SERIAL_PRINTLN("");
    #endif

    
    // copy coinbase hash
    memcpy(mMiner.merkle_result, shaResult, sizeof(shaResult));
    
    byte merkle_concatenated[32 * 2];
    for (size_t k = 0; k < mJob.merkle_branch_len; k++) {
        const char* merkle_element = mJob.merkle_branch[k].c_str();
        uint8_t bytearray[32];
        size_t res = to_byte_array(merkle_element, sizeof(bytearray), bytearray);
        if (res != sizeof(bytearray))
        {
            DEBUG_SERIAL_PRINTLN("Invalid merkle element length");
            return mMiner;
        }

        #ifdef DEBUG_MINING_ALL
        DEBUG_SERIAL_PRINT("    merkle element    "); DEBUG_SERIAL_PRINT(k); DEBUG_SERIAL_PRINT(": "); DEBUG_SERIAL_PRINTLN(merkle_element);
        #endif
        for (size_t i = 0; i < 32; i++) {
          merkle_concatenated[i] = mMiner.merkle_result[i];
          merkle_concatenated[32 + i] = bytearray[i];
        }

        #ifdef DEBUG_MINING_ALL
        DEBUG_SERIAL_PRINT("    merkle element    "); DEBUG_SERIAL_PRINT(k); DEBUG_SERIAL_PRINT(": "); DEBUG_SERIAL_PRINTLN(merkle_element);
        DEBUG_SERIAL_PRINT("    merkle concatenated: ");
        for (size_t i = 0; i < 64; i++)
            DEBUG_SERIAL_PRINTF("%02x", merkle_concatenated[i]);
        DEBUG_SERIAL_PRINTLN("");
        #endif

        mbedtls_sha256_context ctx;
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts_ret(&ctx,0);
        mbedtls_sha256_update_ret(&ctx, merkle_concatenated, 64);
        mbedtls_sha256_finish_ret(&ctx, interResult);

        mbedtls_sha256_starts_ret(&ctx,0);
        mbedtls_sha256_update_ret(&ctx, interResult, 32);
        mbedtls_sha256_finish_ret(&ctx, mMiner.merkle_result);
        mbedtls_sha256_free(&ctx);

        #ifdef DEBUG_MINING_ALL
        DEBUG_SERIAL_PRINT("    merkle sha         : ");
        for (size_t i = 0; i < 32; i++)
            DEBUG_SERIAL_PRINTF("%02x", mMiner.merkle_result[i]);
        DEBUG_SERIAL_PRINTLN("");
        #endif
    }
    // merkle root from merkle_result
    
    // DEBUG_SERIAL_PRINT("    merkle sha         : ");
    char merkle_root[65];
    for (int i = 0; i < 32; i++) {
    //   DEBUG_SERIAL_PRINTF("%02x", mMiner.merkle_result[i]);
      snprintf(&merkle_root[i*2], 3, "%02x", mMiner.merkle_result[i]);
    }
    merkle_root[64] = 0;
    // DEBUG_SERIAL_PRINTLN("");

    // calculate blockheader
    // j.block_header = ''.join([j.version, j.prevhash, merkle_root, j.ntime, j.nbits])
    String blockheader = mJob.version + mJob.prev_block_hash + String(merkle_root) + mJob.ntime + mJob.nbits + "00000000";
    str_len = blockheader.length()/2;
    
    //uint8_t bytearray_blockheader[str_len];
    res = to_byte_array(blockheader.c_str(), sizeof(mMiner.bytearray_blockheader), mMiner.bytearray_blockheader);
    if (res != str_len)
    {
        DEBUG_SERIAL_PRINTLN("Blockheader hex decode failed");
        return mMiner;
    }

    #ifdef DEBUG_MINING_ALL
    DEBUG_SERIAL_PRINTLN("    blockheader: "); DEBUG_SERIAL_PRINT(blockheader);
    DEBUG_SERIAL_PRINTLN("    blockheader bytes "); DEBUG_SERIAL_PRINT(str_len); DEBUG_SERIAL_PRINT(" -> ");
    #endif

    // reverse version
    uint8_t buff;
    size_t bword, bsize, boffset;
    boffset = 0;
    bsize = 4;
    for (size_t j = boffset; j < boffset + (bsize/2); j++) {
        buff = mMiner.bytearray_blockheader[j];
        mMiner.bytearray_blockheader[j] = mMiner.bytearray_blockheader[2 * boffset + bsize - 1 - j];
        mMiner.bytearray_blockheader[2 * boffset + bsize - 1 - j] = buff;
    }

    // reverse prev hash (4-byte word swap)
    boffset = 4;
    bword = 4;
    bsize = 32;
    for (size_t i = 1; i <= bsize / bword; i++) {
        for (size_t j = boffset; j < boffset + bword / 2; j++) {
            buff = mMiner.bytearray_blockheader[j];
            mMiner.bytearray_blockheader[j] = mMiner.bytearray_blockheader[2 * boffset + bword - 1 - j];
            mMiner.bytearray_blockheader[2 * boffset + bword - 1 - j] = buff;
        }
        boffset += bword;
    }

/*
    // reverse merkle (4-byte word swap)
    boffset = 36;
    bword = 4;
    bsize = 32;
    for (size_t i = 1; i <= bsize / bword; i++) {
        for (size_t j = boffset; j < boffset + bword / 2; j++) {
            buff = mMiner.bytearray_blockheader[j];
            mMiner.bytearray_blockheader[j] = mMiner.bytearray_blockheader[2 * boffset + bword - 1 - j];
            mMiner.bytearray_blockheader[2 * boffset + bword - 1 - j] = buff;
        }
        boffset += bword;
    }
*/
    // reverse ntime
    boffset = 68;
    bsize = 4;
    for (size_t j = boffset; j < boffset + (bsize/2); j++) {
        buff = mMiner.bytearray_blockheader[j];
        mMiner.bytearray_blockheader[j] = mMiner.bytearray_blockheader[2 * boffset + bsize - 1 - j];
        mMiner.bytearray_blockheader[2 * boffset + bsize - 1 - j] = buff;
    }

    // reverse difficulty
    boffset = 72;
    bsize = 4;
    for (size_t j = boffset; j < boffset + (bsize/2); j++) {
        buff = mMiner.bytearray_blockheader[j];
        mMiner.bytearray_blockheader[j] = mMiner.bytearray_blockheader[2 * boffset + bsize - 1 - j];
        mMiner.bytearray_blockheader[2 * boffset + bsize - 1 - j] = buff;
    }


    #ifdef DEBUG_MINING_ALL
    DEBUG_SERIAL_PRINT(" >>> bytearray_blockheader     : "); 
    for (size_t i = 0; i < 4; i++)
        DEBUG_SERIAL_PRINTF("%02x", mMiner.bytearray_blockheader[i]);
    DEBUG_SERIAL_PRINTLN("");
    DEBUG_SERIAL_PRINT("version     ");
    for (size_t i = 0; i < 4; i++)
        DEBUG_SERIAL_PRINTF("%02x", mMiner.bytearray_blockheader[i]);
    DEBUG_SERIAL_PRINTLN("");
    DEBUG_SERIAL_PRINT("prev hash   ");
    for (size_t i = 4; i < 4+32; i++)
        DEBUG_SERIAL_PRINTF("%02x", mMiner.bytearray_blockheader[i]);
    DEBUG_SERIAL_PRINTLN("");
    DEBUG_SERIAL_PRINT("merkle root ");
    for (size_t i = 36; i < 36+32; i++)
        DEBUG_SERIAL_PRINTF("%02x", mMiner.bytearray_blockheader[i]);
    DEBUG_SERIAL_PRINTLN("");
    DEBUG_SERIAL_PRINT("ntime       ");
    for (size_t i = 68; i < 68+4; i++)
        DEBUG_SERIAL_PRINTF("%02x", mMiner.bytearray_blockheader[i]);
    DEBUG_SERIAL_PRINTLN("");
    DEBUG_SERIAL_PRINT("nbits       ");
    for (size_t i = 72; i < 72+4; i++)
        DEBUG_SERIAL_PRINTF("%02x", mMiner.bytearray_blockheader[i]);
    DEBUG_SERIAL_PRINTLN("");
    DEBUG_SERIAL_PRINT("nonce       ");
    for (size_t i = 76; i < 76+4; i++)
        DEBUG_SERIAL_PRINTF("%02x", mMiner.bytearray_blockheader[i]);
    DEBUG_SERIAL_PRINTLN("");
    DEBUG_SERIAL_PRINTLN("bytearray_blockheader: ");
    for (size_t i = 0; i < str_len; i++) {
      DEBUG_SERIAL_PRINTF("%02x", mMiner.bytearray_blockheader[i]);
    }
    DEBUG_SERIAL_PRINTLN("");
    #endif
  return mMiner;
}

/* Convert a double value into a truncated string for displaying with its
 * associated suitable for Mega, Giga etc. Buf array needs to be long enough */
void suffix_string(double val, char *buf, size_t bufsiz, int sigdigits)
{
	const double kilo = 1000;
	const double mega = 1000000;
	const double giga = 1000000000;
	const double tera = 1000000000000;
	const double peta = 1000000000000000;
	const double exa  = 1000000000000000000;
	// minimum diff value to display
	const double min_diff = 0.001;
	char suffix[2] = {0,0};
	bool decimal = true;
	double dval;

	// Minimum buffer size needed is 12 bytes: "999.99E" + null = 8, but use 12 for safety
	if (!buf || bufsiz < 12) {
		if (buf && bufsiz > 0)
			buf[0] = '\0';
		return;
	}

	if (val >= exa) {
		val /= peta;
		dval = val / kilo;
        suffix[0] = 'E';
        if (dval > 999.99)
            dval = 999.99;
	} else if (val >= peta) {
		val /= tera;
		dval = val / kilo;
		suffix[0] = 'P';
	} else if (val >= tera) {
		val /= giga;
		dval = val / kilo;
		suffix[0] = 'T';
	} else if (val >= giga) {
		val /= mega;
		dval = val / kilo;
		suffix[0] = 'G';
	} else if (val >= mega) {
		val /= kilo;
		dval = val / kilo;
		suffix[0] = 'M';
	} else if (val >= kilo) {
		dval = val / kilo;
		suffix[0] = 'K';
	} else {
		dval = val;
		if (dval < min_diff)
			dval = 0.0;
	}
    
    int frac = 3;
    if (suffix[0] != 0)
    {
        if (dval > 99.999)
            frac = 1;
        else if (dval > 9.999)
            frac = 2;
    } else
    {
        if (dval > 99.999)
            frac = 2;
        else if (dval > 9.999)
            frac = 3;
        else
            frac = 4;
    }

	if (!sigdigits) {
		if (decimal)
        {
            if (frac == 4)
			    snprintf(buf, bufsiz, "%.4f%s", dval, suffix);
            else if (frac == 3)
			    snprintf(buf, bufsiz, "%.3f%s", dval, suffix);
            else if (frac == 2)
			    snprintf(buf, bufsiz, "%.2f%s", dval, suffix);
            else
			    snprintf(buf, bufsiz, "%.1f%s", dval, suffix);
        } else
			snprintf(buf, bufsiz, "%d%s", (unsigned int)dval, suffix);
	} else {
		/* Always show sigdigits + 1, padded on right with zeroes
		 * followed by suffix */
		int ndigits = sigdigits - 1 - (dval > 0.0 ? floor(log10(dval)) : 0);

		snprintf(buf, bufsiz, "%*.*f%s", sigdigits + 1, ndigits, dval, suffix);
	}
}



#ifndef NO_ROM_CRC32

uint32_t crc32_reset()
{
    return 0xFFFFFFFF;
}

uint32_t crc32_add(uint32_t crc32, const void* data, size_t size)
{
    if (!data || size == 0)
        return crc32;

    return esp_rom_crc32_le(crc32, static_cast<const uint8_t*>(data), static_cast<uint32_t>(size));
}

uint32_t crc32_finish(uint32_t crc32)
{
    return ~crc32;
}

#else

static const uint32_t s_crc32_table[256] =
{
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
    0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
    0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
    0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
    0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
    0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
    0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
    0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
    0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
    0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
    0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
    0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
    0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
    0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
    0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
    0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
    0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
    0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
    0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
    0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
    0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
    0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
    0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
    0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04,
    0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
    0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
    0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
    0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
    0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
    0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
    0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
    0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
    0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
    0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
    0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

uint32_t crc32_reset()
{
    return 0xFFFFFFFF;
}

uint32_t crc32_add(uint32_t crc32, const void* data, size_t size)
{
    if (!data || size == 0)
        return crc32;

    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (size_t n = 0; n < size; ++n)
        crc32 = (crc32 >> 8) ^ s_crc32_table[(crc32 ^ bytes[n]) & 0xFF];
    return crc32;
}

uint32_t crc32_finish(uint32_t crc32)
{
    return crc32 ^ 0xFFFFFFFF;
}

#endif
