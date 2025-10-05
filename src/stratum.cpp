#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "stratum.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "lwip/sockets.h"
#include "utils.h"
#include "version.h"
#include <atomic>
#include "logging.h"



std::atomic<unsigned long> id(1);

// Thread-safe atomic increment (natural wraparound to 0 is acceptable for JSON-RPC ID)
unsigned long getNextId(std::atomic<unsigned long>& id)
{
    return id.fetch_add(1, std::memory_order_relaxed);
}

//Verify Payload doesn't has zero length
bool verifyPayload(const char* line)
{
    if(!line || line[0] == '\0') return false;
    // Check for non-whitespace content
    while(*line) {
        if(*line != ' ' && *line != '\t' && *line != '\n' && *line != '\r')
            return true;
        line++;
    }
    return false;
}

bool checkError(const StaticJsonDocument<BUFFER_JSON_DOC> &doc) 
{
  // Note: doc parameter is passed by value for read-only check
  // Caller must hold s_doc_mutex when calling this function

  if (!doc.containsKey("error")) return false;

  if (doc["error"].size() == 0) return false;

  DEBUG_SERIAL_PRINTF("ERROR: %d | reason: %s \n", (const int) doc["error"][0], (const char*) doc["error"][1]);

  return true;
}


// STEP 1: Pool server connection (SUBSCRIBE)
    // Docs: 
    // - https://cs.braiins.com/stratum-v1/docs
    // - https://github.com/aeternity/protocol/blob/master/STRATUM.md#mining-subscribe
bool tx_mining_subscribe(WiFiClient& client, mining_subscribe& mSubscribe)
{
    char payload[BUFFER] = {0};

    // Subscribe
    id.store(1, std::memory_order_relaxed); //Initialize id messages
    int written = snprintf(payload, BUFFER, "{\"id\": 1, \"method\": \"mining.subscribe\", \"params\": [\"NerdMinerV2/%s\"]}\n", CURRENT_VERSION);

    if (written < 0 || written >= BUFFER) {
        DEBUG_SERIAL_PRINTF("ERROR: Buffer overflow in tx_mining_subscribe\n");
        return false;
    }

    DEBUG_SERIAL_PRINTF("[WORKER] ==> Mining subscribe\n");
    DEBUG_SERIAL_PRINT("  Sending  : "); DEBUG_SERIAL_PRINTLN(payload);
    client.print(payload);
    
    vTaskDelay(200 / portTICK_PERIOD_MS); //Small delay

    String line = client.readStringUntil('\n');
    if(!parse_mining_subscribe(line.c_str(), mSubscribe)) return false;

  
    DEBUG_SERIAL_PRINT("    sub_details: "); DEBUG_SERIAL_PRINTLN(mSubscribe.sub_details);
    DEBUG_SERIAL_PRINT("    extranonce1: "); DEBUG_SERIAL_PRINTLN(mSubscribe.extranonce1);
    DEBUG_SERIAL_PRINT("    extranonce2_size: "); DEBUG_SERIAL_PRINTLN(mSubscribe.extranonce2_size);

    if((mSubscribe.extranonce1.length() == 0) ) {
        DEBUG_SERIAL_PRINTF("[WORKER] >>>>>>>>> Work aborted\n");
        DEBUG_SERIAL_PRINTF("extranonce1 length: %u \n", mSubscribe.extranonce1.length());
        return false;
    }
    return true;
}

bool parse_mining_subscribe(const char* line, mining_subscribe& mSubscribe)
{
    if(!verifyPayload(line)) return false;
    DEBUG_SERIAL_PRINT("  Receiving: "); DEBUG_SERIAL_PRINTLN(line);

    StaticJsonDocument<BUFFER_JSON_DOC> doc;
    DeserializationError error = deserializeJson(doc, line);

    if (error || checkError(doc)) {
        return false;
    }

    if (!doc.containsKey("result")) {
        return false;
    }

    // Validate nested JSON structure before accessing
    if (!doc["result"].is<JsonArray>() || doc["result"].size() < 3) {
        DEBUG_SERIAL_PRINTLN("ERROR: Invalid result array structure");
        return false;
    }

    if (!doc["result"][0].is<JsonArray>() || doc["result"][0].size() < 1) {
        DEBUG_SERIAL_PRINTLN("ERROR: Invalid result[0] array structure");
        return false;
    }

    if (!doc["result"][0][0].is<JsonArray>() || doc["result"][0][0].size() < 2) {
        DEBUG_SERIAL_PRINTLN("ERROR: Invalid result[0][0] array structure");
        return false;
    }

    mSubscribe.sub_details = String((const char*) doc["result"][0][0][1]);
    mSubscribe.extranonce1 = String((const char*) doc["result"][1]);
    mSubscribe.extranonce2_size = doc["result"][2];

    return true;
}

mining_subscribe init_mining_subscribe(void)
{
    mining_subscribe new_mSub;

    new_mSub.extranonce1 = "";
    new_mSub.extranonce2 = "";
    new_mSub.extranonce2_size = 0;
    new_mSub.sub_details = "";


    return new_mSub;
}

// Sanitize input string for JSON payloads (prevents format string attacks and JSON injection)
static bool sanitize_json_string(const char* str, size_t max_len) 
{
    if (!str) return false;

    size_t len = strnlen(str, max_len + 1);
    if (len >= max_len) return false;  // String too long

    // Check for dangerous characters that could break JSON or be format strings
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        // Reject control characters, quotes, backslashes, and format specifiers
        if (c < 0x20 || c == '"' || c == '\\' || c == '%') {
            return false;
        }
    }
    return true;
}

// STEP 2: Pool server auth (authorize)
bool tx_mining_auth(WiFiClient& client, const char * user, const char * pass)
{
    char payload[BUFFER] = {0};

    // Sanitize user and password inputs
    if (!sanitize_json_string(user, 128)) {
        DEBUG_SERIAL_PRINTF("ERROR: Invalid username for mining.authorize\n");
        return false;
    }
    if (!sanitize_json_string(pass, 64)) {
        DEBUG_SERIAL_PRINTF("ERROR: Invalid password for mining.authorize\n");
        return false;
    }

    // Authorize
    unsigned long msg_id = getNextId(id);
    int written = snprintf(payload, BUFFER, "{\"id\": %lu, \"method\": \"mining.authorize\", \"params\": [\"%s\", \"%s\"]}\n",
      msg_id, user, pass);

    if (written < 0 || written >= BUFFER) {
        DEBUG_SERIAL_PRINTF("ERROR: Buffer overflow in tx_mining_auth\n");
        return false;
    }

    DEBUG_SERIAL_PRINTF("[WORKER] ==> Autorize work\n");
    DEBUG_SERIAL_PRINT("  Sending  : "); DEBUG_SERIAL_PRINTLN(payload);
    client.print(payload);

    vTaskDelay(200 / portTICK_PERIOD_MS); //Small delay

    //Don't parse here any answer
    //Miner started to receive mining notifications so better parse all at main thread

    return true;
}

stratum_method parse_mining_method(const char* line)
{
    if(!verifyPayload(line)) return STRATUM_PARSE_ERROR;
    DEBUG_SERIAL_PRINT("  Receiving: "); DEBUG_SERIAL_PRINTLN(line);

    StaticJsonDocument<BUFFER_JSON_DOC> doc;
    DeserializationError error = deserializeJson(doc, line);

    if (error) {
        DEBUG_SERIAL_PRINTF("ERROR: JSON parse failed: %s\n", error.c_str());
        return STRATUM_PARSE_ERROR;
    }

    if (checkError(doc)) {
        return STRATUM_PARSE_ERROR;
    }

    if (!doc.containsKey("method")) {
      // "error":null means success
      if (doc["error"].isNull())
        return STRATUM_SUCCESS;
      else
        return STRATUM_UNKNOWN;
    }

    const char* method = doc["method"];
    if (!method) {
        DEBUG_SERIAL_PRINTLN("ERROR: method field is null");
        return STRATUM_PARSE_ERROR;
    }

    // Check most common methods first
    if (strncmp("mining.notify", method, 13) == 0 && method[13] == '\0') {
        return MINING_NOTIFY;
    }

    if (strncmp("mining.set_difficulty", method, 21) == 0 && method[21] == '\0') {
        return MINING_SET_DIFFICULTY;
    }

    // Log unknown methods for debugging
    DEBUG_SERIAL_PRINTF("WARNING: Unknown stratum method: %s\n", method);
    return STRATUM_UNKNOWN;
}

bool parse_mining_notify(const char* line, mining_job& mJob)
{
    DEBUG_SERIAL_PRINTLN("    Parsing Method [MINING NOTIFY]");
    if(!verifyPayload(line)) return false;

    StaticJsonDocument<BUFFER_JSON_DOC> doc;
    DeserializationError error = deserializeJson(doc, line);

    if (error)
    {
        return false;
    }

    // Check for errors BEFORE processing any data
    if (checkError(doc))
    {
        DEBUG_SERIAL_PRINTF("[WORKER] >>>>>>>>> Work aborted - error in response\n");
        return false;
    }

    if (!doc.containsKey("params"))
    {
        return false;
    }

    // Validate params array size
    if (!doc["params"].is<JsonArray>() || doc["params"].size() < 9)
    {
        DEBUG_SERIAL_PRINTLN("ERROR: Invalid params array in mining.notify");
        return false;
    }

    JsonArray params = doc["params"].as<JsonArray>();

    // Validate and extract string params in single pass (indices: 0,1,2,3,5,6,7)
    const char* job_id_ptr;
    const char* prev_hash_ptr;
    const char* coinb1_ptr;
    const char* coinb2_ptr;
    const char* version_ptr;
    const char* nbits_ptr;
    const char* ntime_ptr;

    if (!params[0].is<const char*>() || !(job_id_ptr = params[0].as<const char*>()))
    {
        DEBUG_SERIAL_PRINTLN("ERROR: Invalid job_id");
        return false;
    }
    if (!params[1].is<const char*>() || !(prev_hash_ptr = params[1].as<const char*>()))
    {
        DEBUG_SERIAL_PRINTLN("ERROR: Invalid prev_block_hash");
        return false;
    }
    if (!params[2].is<const char*>() || !(coinb1_ptr = params[2].as<const char*>()))
    {
        DEBUG_SERIAL_PRINTLN("ERROR: Invalid coinb1");
        return false;
    }
    if (!params[3].is<const char*>() || !(coinb2_ptr = params[3].as<const char*>()))
    {
        DEBUG_SERIAL_PRINTLN("ERROR: Invalid coinb2");
        return false;
    }
    if (!params[5].is<const char*>() || !(version_ptr = params[5].as<const char*>()))
    {
        DEBUG_SERIAL_PRINTLN("ERROR: Invalid version");
        return false;
    }
    if (!params[6].is<const char*>() || !(nbits_ptr = params[6].as<const char*>()))
    {
        DEBUG_SERIAL_PRINTLN("ERROR: Invalid nbits");
        return false;
    }
    if (!params[7].is<const char*>() || !(ntime_ptr = params[7].as<const char*>()))
    {
        DEBUG_SERIAL_PRINTLN("ERROR: Invalid ntime");
        return false;
    }

    // Validate clean_jobs boolean
    if (!params[8].is<bool>())
    {
        DEBUG_SERIAL_PRINTLN("ERROR: clean_jobs is not a boolean");
        return false;
    }

    // Validate and process merkle_branch
    mJob.merkle_branch_len = 0;
    if (params[4].is<JsonArray>())
    {
        JsonArray merkle_array = params[4].as<JsonArray>();
        size_t merkle_size = merkle_array.size();

        if (merkle_size > MAX_MERKLE_BRANCHES)
        {
            DEBUG_SERIAL_PRINTF("ERROR: Merkle branch too large: %u > %u\n", merkle_size, MAX_MERKLE_BRANCHES);
            return false;
        }

        for (size_t idx = 0; idx < merkle_size; ++idx)
        {
            if (!merkle_array[idx].is<const char*>())
            {
                DEBUG_SERIAL_PRINTF("ERROR: Merkle branch entry %u is not a string\n", idx);
                return false;
            }

            const char* branch_entry = merkle_array[idx].as<const char*>();
            if (!branch_entry)
            {
                DEBUG_SERIAL_PRINTLN("ERROR: Null merkle branch entry");
                return false;
            }

            mJob.merkle_branch[idx] = String(branch_entry);
        }
        mJob.merkle_branch_len = merkle_size;
    }
    else if (!params[4].isNull())
    {
        DEBUG_SERIAL_PRINTLN("ERROR: Invalid merkle_branch structure");
        return false;
    }

    // All validations passed - now safely construct and assign
    mJob.job_id = String(job_id_ptr);
    mJob.prev_block_hash = String(prev_hash_ptr);
    mJob.coinb1 = String(coinb1_ptr);
    mJob.coinb2 = String(coinb2_ptr);
    mJob.version = String(version_ptr);
    mJob.nbits = String(nbits_ptr);
    mJob.ntime = String(ntime_ptr);
    mJob.clean_jobs = params[8].as<bool>();

    #ifdef DEBUG_MINING
    DEBUG_SERIAL_PRINT("    job_id: "); DEBUG_SERIAL_PRINTLN(mJob.job_id);
    DEBUG_SERIAL_PRINT("    prevhash: "); DEBUG_SERIAL_PRINTLN(mJob.prev_block_hash);
    DEBUG_SERIAL_PRINT("    coinb1: "); DEBUG_SERIAL_PRINTLN(mJob.coinb1);
    DEBUG_SERIAL_PRINT("    coinb2: "); DEBUG_SERIAL_PRINTLN(mJob.coinb2);
    DEBUG_SERIAL_PRINT("    merkle_branch size: "); DEBUG_SERIAL_PRINTLN(mJob.merkle_branch_len);
    DEBUG_SERIAL_PRINT("    version: "); DEBUG_SERIAL_PRINTLN(mJob.version);
    DEBUG_SERIAL_PRINT("    nbits: "); DEBUG_SERIAL_PRINTLN(mJob.nbits);
    DEBUG_SERIAL_PRINT("    ntime: "); DEBUG_SERIAL_PRINTLN(mJob.ntime);
    DEBUG_SERIAL_PRINT("    clean_jobs: "); DEBUG_SERIAL_PRINTLN(mJob.clean_jobs);
    #endif

    return true;
}

bool tx_mining_submit(WiFiClient& client, mining_subscribe mWorker, mining_job mJob, unsigned long nonce, unsigned long &submit_id)
{
    char payload[BUFFER] = {0};

    // Validate wName is null-terminated and not too long
    size_t wname_len = strnlen(mWorker.wName, sizeof(mWorker.wName));
    if (wname_len >= sizeof(mWorker.wName))
    {
        DEBUG_SERIAL_PRINTF("ERROR: wName not properly null-terminated\n");
        return false;
    }

    // Validate client is connected
    if (!client.connected())
    {
        DEBUG_SERIAL_PRINTF("ERROR: Client not connected\n");
        return false;
    }

    // Submit
    submit_id = getNextId(id);

    char nonce_hex[9] = {0};
    int nonce_written = snprintf(nonce_hex, sizeof(nonce_hex), "%08lx", nonce);
    if (nonce_written < 0 || nonce_written >= static_cast<int>(sizeof(nonce_hex)))
    {
        DEBUG_SERIAL_PRINTF("ERROR: Failed to format nonce\n");
        return false;
    }

    int written = snprintf(payload, BUFFER, "{\"id\": %lu, \"method\": \"mining.submit\", \"params\": [\"%s\", \"%s\", \"%s\", \"%s\", \"%s\"]}\n",
        submit_id,
        mWorker.wName,
        mJob.job_id.c_str(),
        mWorker.extranonce2.c_str(),
        mJob.ntime.c_str(),
        nonce_hex
    );

    if (written < 0 || written >= BUFFER)
    {
        DEBUG_SERIAL_PRINTF("ERROR: Buffer overflow in tx_mining_submit (needed %d, have %d)\n", written, BUFFER);
        return false;
    }

    DEBUG_SERIAL_PRINT("  Sending  : "); DEBUG_SERIAL_PRINT(payload);

    size_t bytes_written = client.print(payload);
    if (bytes_written != strlen(payload))
    {
        DEBUG_SERIAL_PRINTF("ERROR: Failed to send complete payload (%zu/%zu bytes)\n", bytes_written, strlen(payload));
        return false;
    }

    return true;
}

bool parse_mining_set_difficulty(const char* line, double& difficulty)
{
    DEBUG_SERIAL_PRINTLN("    Parsing Method [SET DIFFICULTY]");
    if(!verifyPayload(line)) return false;

    StaticJsonDocument<BUFFER_JSON_DOC> doc;
    DeserializationError error = deserializeJson(doc, line);

    if (error) {
        return false;
    }

    if (!doc.containsKey("params")) {
        return false;
    }

    // Validate params is array with at least 1 element
    if (!doc["params"].is<JsonArray>() || doc["params"].size() < 1) {
        DEBUG_SERIAL_PRINTLN("ERROR: Invalid params array in mining.set_difficulty");
        return false;
    }

    DEBUG_SERIAL_PRINT("    difficulty: "); DEBUG_SERIAL_PRINTLN((double)doc["params"][0],12);
    difficulty = (double)doc["params"][0];

    return true;
}

bool tx_suggest_difficulty(WiFiClient& client, double difficulty)
{
    char payload[BUFFER] = {0};

    unsigned long msg_id = getNextId(id);
    int written = snprintf(payload, BUFFER, "{\"id\": %lu, \"method\": \"mining.suggest_difficulty\", \"params\": [%.10g]}\n", msg_id, difficulty);

    if (written < 0 || written >= BUFFER) {
        DEBUG_SERIAL_PRINTF("ERROR: Buffer overflow in tx_suggest_difficulty\n");
        return false;
    }

    DEBUG_SERIAL_PRINT("  Sending  : "); DEBUG_SERIAL_PRINT(payload);
    size_t bytes_written = client.print(payload);
    return bytes_written > 0;

}

unsigned long parse_extract_id(const char* line)
{
    StaticJsonDocument<BUFFER_JSON_DOC> doc;
    DeserializationError error = deserializeJson(doc, line);
    if (error) 
    {
        return 0;
    }

    if (!doc.containsKey("id")) 
    {
        return 0;
    }

    // Validate id is numeric type before casting
    if (!doc["id"].is<unsigned long>()) 
    {
        DEBUG_SERIAL_PRINTLN("ERROR: id field is not numeric");
        return 0;
    }

    unsigned long extracted_id = doc["id"];
    return extracted_id;
}
