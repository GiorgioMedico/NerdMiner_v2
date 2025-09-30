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
#include <mutex>



unsigned long id = 1;

// Thread safety mutex for id counter
static std::mutex s_id_mutex;

//Get next JSON RPC Id
unsigned long getNextId(unsigned long& id) {
    std::lock_guard<std::mutex> lock(s_id_mutex);
    if (id == ULONG_MAX) {
      id = 1;
      return id;
    }
    return ++id;
}

//Verify Payload doesn't has zero length
bool verifyPayload (const String& line){
  if(line.length() == 0) return false;

  // Check if string contains only whitespace
  String trimmed = line;
  trimmed.trim();
  if(trimmed.isEmpty()) return false;

  return true;
}

bool checkError(const StaticJsonDocument<BUFFER_JSON_DOC> doc) {
  // Note: doc parameter is passed by value for read-only check
  // Caller must hold s_doc_mutex when calling this function

  if (!doc.containsKey("error")) return false;

  if (doc["error"].size() == 0) return false;

  Serial.printf("ERROR: %d | reason: %s \n", (const int) doc["error"][0], (const char*) doc["error"][1]);

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
    id = 1; //Initialize id messages
    #ifndef HAN
    int written = snprintf(payload, BUFFER, "{\"id\": %u, \"method\": \"mining.subscribe\", \"params\": [\"NerdMinerV2/%s\"]}\n", id, CURRENT_VERSION);
    #else
    int written = snprintf(payload, BUFFER, "{\"id\": %u, \"method\": \"mining.subscribe\", \"params\": [\"HAN_SOLOminer/%s\"]}\n", id, CURRENT_VERSION);
    #endif

    if (written < 0 || written >= BUFFER) {
        Serial.printf("ERROR: Buffer overflow in tx_mining_subscribe\n");
        return false;
    }

    Serial.printf("[WORKER] ==> Mining subscribe\n");
    Serial.print("  Sending  : "); Serial.println(payload);
    client.print(payload);
    
    vTaskDelay(200 / portTICK_PERIOD_MS); //Small delay
    
    String line = client.readStringUntil('\n');
    if(!parse_mining_subscribe(line, mSubscribe)) return false;

  
    Serial.print("    sub_details: "); Serial.println(mSubscribe.sub_details);
    Serial.print("    extranonce1: "); Serial.println(mSubscribe.extranonce1);
    Serial.print("    extranonce2_size: "); Serial.println(mSubscribe.extranonce2_size);

    if((mSubscribe.extranonce1.length() == 0) ) {
        Serial.printf("[WORKER] >>>>>>>>> Work aborted\n");
        Serial.printf("extranonce1 length: %u \n", mSubscribe.extranonce1.length());
        return false;
    }
    return true;
}

bool parse_mining_subscribe(const String& line, mining_subscribe& mSubscribe)
{
    if(!verifyPayload(line)) return false;
    Serial.print("  Receiving: "); Serial.println(line);

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
        Serial.println("ERROR: Invalid result array structure");
        return false;
    }

    if (!doc["result"][0].is<JsonArray>() || doc["result"][0].size() < 1) {
        Serial.println("ERROR: Invalid result[0] array structure");
        return false;
    }

    if (!doc["result"][0][0].is<JsonArray>() || doc["result"][0][0].size() < 2) {
        Serial.println("ERROR: Invalid result[0][0] array structure");
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
static bool sanitize_json_string(const char* str, size_t max_len) {
    if (!str) return false;

    size_t len = strnlen(str, max_len + 1);
    if (len > max_len) return false;  // String too long

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
        Serial.printf("ERROR: Invalid username for mining.authorize\n");
        return false;
    }
    if (!sanitize_json_string(pass, 64)) {
        Serial.printf("ERROR: Invalid password for mining.authorize\n");
        return false;
    }

    // Authorize
    id = getNextId(id);
    int written = snprintf(payload, BUFFER, "{\"params\": [\"%s\", \"%s\"], \"id\": %u, \"method\": \"mining.authorize\"}\n",
      user, pass, id);

    if (written < 0 || written >= BUFFER) {
        Serial.printf("ERROR: Buffer overflow in tx_mining_auth\n");
        return false;
    }

    Serial.printf("[WORKER] ==> Autorize work\n");
    Serial.print("  Sending  : "); Serial.println(payload);
    client.print(payload);

    vTaskDelay(200 / portTICK_PERIOD_MS); //Small delay

    //Don't parse here any answer
    //Miner started to receive mining notifications so better parse all at main thread

    return true;
}


stratum_method parse_mining_method(const String& line)
{
    if(!verifyPayload(line)) return STRATUM_PARSE_ERROR;
    Serial.print("  Receiving: "); Serial.println(line);

    StaticJsonDocument<BUFFER_JSON_DOC> doc;
    DeserializationError error = deserializeJson(doc, line);

    if (error || checkError(doc)) {
        return STRATUM_PARSE_ERROR;
    }

    if (!doc.containsKey("method")) {
      // "error":null means success
      if (doc["error"].isNull())
        return STRATUM_SUCCESS;
      else
        return STRATUM_UNKNOWN;
    }

    stratum_method result = STRATUM_UNKNOWN;

    if (strcmp("mining.notify", (const char*) doc["method"]) == 0) {
        result = MINING_NOTIFY;
    } else if (strcmp("mining.set_difficulty", (const char*) doc["method"]) == 0) {
        result = MINING_SET_DIFFICULTY;
    }

    return result;
}

bool parse_mining_notify(const String& line, mining_job& mJob)
{
    Serial.println("    Parsing Method [MINING NOTIFY]");
    if(!verifyPayload(line)) return false;

    StaticJsonDocument<BUFFER_JSON_DOC> doc;
    DeserializationError error = deserializeJson(doc, line);

    if (error) {
        return false;
    }

    if (!doc.containsKey("params")) {
        return false;
    }

    // Validate params array size
    if (!doc["params"].is<JsonArray>() || doc["params"].size() < 9) {
        Serial.println("ERROR: Invalid params array in mining.notify");
        return false;
    }

    mJob.merkle_branch_len = 0;
    // Validate merkle_branch size
    if (doc["params"][4].is<JsonArray>()) {
        JsonArray merkle_array = doc["params"][4].as<JsonArray>();
        size_t merkle_size = merkle_array.size();
        if (merkle_size > MAX_MERKLE_BRANCHES) {
            Serial.printf("ERROR: Merkle branch too large: %u > %u\n", merkle_size, MAX_MERKLE_BRANCHES);
            return false;
        }
        for (size_t idx = 0; idx < merkle_size; ++idx) {
            const char* branch_entry = merkle_array[idx];
            if (!branch_entry) {
                Serial.println("ERROR: Null merkle branch entry");
                return false;
            }
            String branch_value(branch_entry);
            if (branch_value.length() > MAX_HASH_LEN) {
                Serial.println("ERROR: Merkle branch entry too long");
                return false;
            }
            mJob.merkle_branch[idx] = branch_value;
        }
        mJob.merkle_branch_len = merkle_size;
    } else if (!doc["params"][4].isNull()) {
        Serial.println("ERROR: Invalid merkle_branch structure");
        return false;
    }

    // Limit string lengths to prevent memory exhaustion
    String job_id = String((const char*) doc["params"][0]);
    String prev_block_hash = String((const char*) doc["params"][1]);
    String coinb1 = String((const char*) doc["params"][2]);
    String coinb2 = String((const char*) doc["params"][3]);
    String version = String((const char*) doc["params"][5]);
    String nbits = String((const char*) doc["params"][6]);
    String ntime = String((const char*) doc["params"][7]);

    if (job_id.length() > MAX_JOB_ID_LEN || prev_block_hash.length() > MAX_HASH_LEN ||
        coinb1.length() > MAX_COINBASE_LEN || coinb2.length() > MAX_COINBASE_LEN ||
        version.length() > MAX_VERSION_LEN || nbits.length() > MAX_NBITS_LEN || ntime.length() > MAX_NTIME_LEN) {
        Serial.println("ERROR: String field too long in mining.notify");
        return false;
    }

    mJob.job_id = job_id;
    mJob.prev_block_hash = prev_block_hash;
    mJob.coinb1 = coinb1;
    mJob.coinb2 = coinb2;
    mJob.version = version;
    mJob.nbits = nbits;
    mJob.ntime = ntime;
    mJob.clean_jobs = doc["params"][8]; //bool

    #ifdef DEBUG_MINING
    Serial.print("    job_id: "); Serial.println(mJob.job_id);
    Serial.print("    prevhash: "); Serial.println(mJob.prev_block_hash);
    Serial.print("    coinb1: "); Serial.println(mJob.coinb1);
    Serial.print("    coinb2: "); Serial.println(mJob.coinb2);
    Serial.print("    merkle_branch size: "); Serial.println(mJob.merkle_branch_len);
    Serial.print("    version: "); Serial.println(mJob.version);
    Serial.print("    nbits: "); Serial.println(mJob.nbits);
    Serial.print("    ntime: "); Serial.println(mJob.ntime);
    Serial.print("    clean_jobs: "); Serial.println(mJob.clean_jobs);
    #endif

    //Check if parameters where correctly received
    if (checkError(doc)) {
      Serial.printf("[WORKER] >>>>>>>>> Work aborted\n");
      return false;
    }

    return true;
}


bool tx_mining_submit(WiFiClient& client, mining_subscribe mWorker, mining_job mJob, unsigned long nonce, unsigned long &submit_id)
{
    char payload[BUFFER] = {0};

    // Validate wName is null-terminated and not too long
    size_t wname_len = strnlen(mWorker.wName, sizeof(mWorker.wName));
    if (wname_len >= sizeof(mWorker.wName)) {
        Serial.printf("ERROR: wName not properly null-terminated\n");
        return false;
    }

    // Submit
    id = getNextId(id);
    submit_id = id;

    char nonce_hex[9] = {0};
    int nonce_written = snprintf(nonce_hex, sizeof(nonce_hex), "%08lx", nonce);
    if (nonce_written < 0 || nonce_written >= static_cast<int>(sizeof(nonce_hex))) {
        Serial.printf("ERROR: Failed to format nonce\n");
        return false;
    }

    int written = snprintf(payload, BUFFER, "{\"id\":%u,\"method\":\"mining.submit\",\"params\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"]}\n",
        id,
        mWorker.wName,
        mJob.job_id.c_str(),
        mWorker.extranonce2.c_str(),
        mJob.ntime.c_str(),
        nonce_hex
        );

    if (written < 0 || written >= BUFFER) {
        Serial.printf("ERROR: Buffer overflow in tx_mining_submit\n");
        return false;
    }

    Serial.print("  Sending  : "); Serial.print(payload);
    client.print(payload);
    //Serial.print("  Receiving: "); Serial.println(client.readStringUntil('\n'));

    return true;
}

bool parse_mining_set_difficulty(const String& line, double& difficulty)
{
    Serial.println("    Parsing Method [SET DIFFICULTY]");
    if(!verifyPayload(line)) return false;

    StaticJsonDocument<BUFFER_JSON_DOC> doc;
    DeserializationError error = deserializeJson(doc, line);

    if (error) {
        return false;
    }

    if (!doc.containsKey("params")) {
        return false;
    }

    Serial.print("    difficulty: "); Serial.println((double)doc["params"][0],12);
    difficulty = (double)doc["params"][0];

    return true;
}

bool tx_suggest_difficulty(WiFiClient& client, double difficulty)
{
    char payload[BUFFER] = {0};

    id = getNextId(id);
    int written = snprintf(payload, BUFFER, "{\"id\":%d,\"method\":\"mining.suggest_difficulty\",\"params\":[%.10g]}\n", id, difficulty);

    if (written < 0 || written >= BUFFER) {
        Serial.printf("ERROR: Buffer overflow in tx_suggest_difficulty\n");
        return false;
    }

    Serial.print("  Sending  : "); Serial.print(payload);
    return client.print(payload);

}


unsigned long parse_extract_id(const String &line)
{
    StaticJsonDocument<BUFFER_JSON_DOC> doc;
    DeserializationError error = deserializeJson(doc, line);
    if (error) {
        return 0;
    }

    if (!doc.containsKey("id")) {
        return 0;
    }

    unsigned long id = doc["id"];
    return id;
}
