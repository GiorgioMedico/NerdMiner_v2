#ifndef STRATUM_API_H
#define STRATUM_API_H

#include "cJSON.h"
#include <stdint.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#define MAX_MERKLE_BRANCHES 32
#define HASH_SIZE 32
#define COINBASE_SIZE 100
#define COINBASE2_SIZE 128

#define BUFFER_JSON_DOC 4096
#define BUFFER 1024

// String length limits for validation
#define MAX_JOB_ID_LEN 256
#define MAX_HASH_LEN 256
#define MAX_COINBASE_LEN 512
#define MAX_VERSION_LEN 64
#define MAX_NBITS_LEN 64
#define MAX_NTIME_LEN 64

typedef struct {
    String sub_details;
    String extranonce1;
    String extranonce2;
    int extranonce2_size;
    char wName[80];
    char wPass[20];
} mining_subscribe;

typedef struct {
    String job_id;
    String prev_block_hash;
    String coinb1;
    String coinb2;
    String nbits;
    size_t merkle_branch_len = 0;
    String merkle_branch[MAX_MERKLE_BRANCHES];
    String version;
    uint32_t target;
    String ntime;
    bool clean_jobs;
} mining_job;

typedef enum {
    STRATUM_SUCCESS,
    STRATUM_UNKNOWN,
    STRATUM_PARSE_ERROR,
    MINING_NOTIFY,
    MINING_SET_DIFFICULTY
} stratum_method;

typedef enum {
    STRATUM_ERR_OK = 0,
    STRATUM_ERR_BUFFER_OVERFLOW,
    STRATUM_ERR_INVALID_JSON,
    STRATUM_ERR_VALIDATION_FAILED,
    STRATUM_ERR_MALFORMED_PAYLOAD
} stratum_error;

unsigned long getNextId(unsigned long& id);
bool verifyPayload (const String& line);
bool checkError(const StaticJsonDocument<BUFFER_JSON_DOC> &doc);

//Method Mining.subscribe
mining_subscribe init_mining_subscribe(void);
bool tx_mining_subscribe(WiFiClient& client, mining_subscribe& mSubscribe);
bool parse_mining_subscribe(const String& line, mining_subscribe& mSubscribe);

//Method Mining.authorise
bool tx_mining_auth(WiFiClient& client, const char * user, const char * pass);
stratum_method parse_mining_method(const String& line);
bool parse_mining_notify(const String& line, mining_job& mJob);

//Method Mining.submit
bool tx_mining_submit(WiFiClient& client, mining_subscribe mWorker, mining_job mJob, unsigned long nonce, unsigned long &submit_id);

//Difficulty Methods
bool tx_suggest_difficulty(WiFiClient& client, double difficulty);
bool parse_mining_set_difficulty(const String& line, double& difficulty);

unsigned long parse_extract_id(const String &line);

#endif // STRATUM_API_H
