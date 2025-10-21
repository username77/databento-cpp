#ifndef DATABENTO_C_API_H
#define DATABENTO_C_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Error handling ------------------------------------------------------------

// Returns the message from the last API call that failed on the current
// thread. The pointer is valid until the next API call on the same thread.
// Returns NULL if no error was recorded.
const char* db_c_last_error(void);

// Forward-declared handle types --------------------------------------------

typedef struct db_c_live_builder db_c_live_builder_t;
typedef struct db_c_live_threaded db_c_live_threaded_t;

// Enumerations --------------------------------------------------------------

typedef enum db_c_dataset {
  DB_C_DATASET_GLBX_MDP3 = 1,
  DB_C_DATASET_XNAS_ITCH = 2,
  DB_C_DATASET_XBOS_ITCH = 3,
  DB_C_DATASET_XPSX_ITCH = 4,
  DB_C_DATASET_BATS_PITCH = 5,
  DB_C_DATASET_BATY_PITCH = 6,
  DB_C_DATASET_EDGA_PITCH = 7,
  DB_C_DATASET_EDGX_PITCH = 8,
  DB_C_DATASET_XNYS_PILLAR = 9,
  DB_C_DATASET_XCIS_PILLAR = 10,
  DB_C_DATASET_XASE_PILLAR = 11,
  DB_C_DATASET_XCHI_PILLAR = 12,
  DB_C_DATASET_XCIS_BBO = 13,
  DB_C_DATASET_XCIS_TRADES = 14,
  DB_C_DATASET_MEMX_MEMOIR = 15,
  DB_C_DATASET_EPRL_DOM = 16,
  DB_C_DATASET_FINN_NLS = 17,
  DB_C_DATASET_FINY_TRADES = 18,
  DB_C_DATASET_OPRA_PILLAR = 19,
  DB_C_DATASET_DBEQ_BASIC = 20,
  DB_C_DATASET_ARCX_PILLAR = 21,
  DB_C_DATASET_IEXG_TOPS = 22,
  DB_C_DATASET_EQUS_PLUS = 23,
  DB_C_DATASET_XNYS_BBO = 24,
  DB_C_DATASET_XNYS_TRADES = 25,
  DB_C_DATASET_XNAS_QBBO = 26,
  DB_C_DATASET_XNAS_NLS = 27,
  DB_C_DATASET_IFEU_IMPACT = 28,
  DB_C_DATASET_NDEX_IMPACT = 29,
  DB_C_DATASET_EQUS_ALL = 30,
  DB_C_DATASET_XNAS_BASIC = 31,
  DB_C_DATASET_EQUS_SUMMARY = 32,
  DB_C_DATASET_XCIS_TRADESBBO = 33,
  DB_C_DATASET_XNYS_TRADESBBO = 34,
  DB_C_DATASET_EQUS_MINI = 35,
  DB_C_DATASET_IFUS_IMPACT = 36,
  DB_C_DATASET_IFLL_IMPACT = 37,
  DB_C_DATASET_XEUR_EOBI = 38,
  DB_C_DATASET_XEEE_EOBI = 39,
} db_c_dataset_t;

typedef enum db_c_schema {
  DB_C_SCHEMA_MBO = 0,
  DB_C_SCHEMA_MBP_1 = 1,
  DB_C_SCHEMA_MBP_10 = 2,
  DB_C_SCHEMA_TBBO = 3,
  DB_C_SCHEMA_TRADES = 4,
  DB_C_SCHEMA_OHLCV_1S = 5,
  DB_C_SCHEMA_OHLCV_1M = 6,
  DB_C_SCHEMA_OHLCV_1H = 7,
  DB_C_SCHEMA_OHLCV_1D = 8,
  DB_C_SCHEMA_DEFINITION = 9,
  DB_C_SCHEMA_STATISTICS = 10,
  DB_C_SCHEMA_STATUS = 11,
  DB_C_SCHEMA_IMBALANCE = 12,
  DB_C_SCHEMA_OHLCV_EOD = 13,
  DB_C_SCHEMA_CMBP_1 = 14,
  DB_C_SCHEMA_CBBO_1S = 15,
  DB_C_SCHEMA_CBBO_1M = 16,
  DB_C_SCHEMA_TCBBO = 17,
  DB_C_SCHEMA_BBO_1S = 18,
  DB_C_SCHEMA_BBO_1M = 19,
} db_c_schema_t;

typedef enum db_c_stype {
  DB_C_STYPE_INSTRUMENT_ID = 0,
  DB_C_STYPE_RAW_SYMBOL = 1,
  DB_C_STYPE_SMART = 2,
  DB_C_STYPE_CONTINUOUS = 3,
  DB_C_STYPE_PARENT = 4,
  DB_C_STYPE_NASDAQ_SYMBOL = 5,
  DB_C_STYPE_CMS_SYMBOL = 6,
  DB_C_STYPE_ISIN = 7,
  DB_C_STYPE_US_CODE = 8,
  DB_C_STYPE_BBG_COMP_ID = 9,
  DB_C_STYPE_BBG_COMP_TICKER = 10,
  DB_C_STYPE_FIGI = 11,
  DB_C_STYPE_FIGI_TICKER = 12,
} db_c_stype_t;

typedef enum db_c_upgrade_policy {
  DB_C_UPGRADE_AS_IS = 0,
  DB_C_UPGRADE_TO_V2 = 1,
  DB_C_UPGRADE_TO_V3 = 2,
} db_c_upgrade_policy_t;

typedef enum db_c_keep_going {
  DB_C_KEEP_GOING_CONTINUE = 0,
  DB_C_KEEP_GOING_STOP = 1,
} db_c_keep_going_t;

typedef enum db_c_exception_action {
  DB_C_EXCEPTION_ACTION_RESTART = 0,
  DB_C_EXCEPTION_ACTION_STOP = 1,
} db_c_exception_action_t;

// Record interop ------------------------------------------------------------

typedef struct db_c_record_header {
  uint8_t length_words;
  uint8_t rtype;
  uint16_t publisher_id;
  uint32_t instrument_id;
  uint64_t ts_event;
} db_c_record_header_t;

typedef db_c_keep_going_t (*db_c_record_callback_t)(void* user_data,
                                                     const db_c_record_header_t* header,
                                                     const void* body,
                                                     size_t body_size);

typedef void (*db_c_metadata_callback_t)(void* user_data, const char* metadata_text);

typedef db_c_exception_action_t (*db_c_exception_callback_t)(void* user_data,
                                                              const char* message);

// LiveBuilder API -----------------------------------------------------------

db_c_live_builder_t* db_c_live_builder_new(void);
void db_c_live_builder_free(db_c_live_builder_t* builder);

int db_c_live_builder_set_key(db_c_live_builder_t* builder, const char* api_key);
int db_c_live_builder_set_key_from_env(db_c_live_builder_t* builder);
int db_c_live_builder_set_dataset(db_c_live_builder_t* builder, const char* dataset_code);
int db_c_live_builder_set_dataset_enum(db_c_live_builder_t* builder, db_c_dataset_t dataset);
int db_c_live_builder_set_send_ts_out(db_c_live_builder_t* builder, int send_ts_out);
int db_c_live_builder_set_upgrade_policy(db_c_live_builder_t* builder,
                                         db_c_upgrade_policy_t policy);
int db_c_live_builder_set_heartbeat_interval(db_c_live_builder_t* builder, uint32_t seconds);
int db_c_live_builder_set_address(db_c_live_builder_t* builder, const char* gateway,
                                  uint16_t port);
int db_c_live_builder_set_buffer_size(db_c_live_builder_t* builder, size_t size);

db_c_live_threaded_t* db_c_live_builder_build_threaded(db_c_live_builder_t* builder);

// LiveThreaded API ---------------------------------------------------------

void db_c_live_threaded_free(db_c_live_threaded_t* client);

int db_c_live_threaded_subscribe(db_c_live_threaded_t* client,
                                 const char* const* symbols,
                                 size_t symbol_count,
                                 db_c_schema_t schema,
                                 db_c_stype_t stype_in);

int db_c_live_threaded_subscribe_from_unix(db_c_live_threaded_t* client,
                                           const char* const* symbols,
                                           size_t symbol_count,
                                           db_c_schema_t schema,
                                           db_c_stype_t stype_in,
                                           uint64_t start_unix_nanos);

int db_c_live_threaded_subscribe_from_str(db_c_live_threaded_t* client,
                                          const char* const* symbols,
                                          size_t symbol_count,
                                          db_c_schema_t schema,
                                          db_c_stype_t stype_in,
                                          const char* start);

int db_c_live_threaded_subscribe_with_snapshot(db_c_live_threaded_t* client,
                                               const char* const* symbols,
                                               size_t symbol_count,
                                               db_c_schema_t schema,
                                               db_c_stype_t stype_in);

int db_c_live_threaded_start(db_c_live_threaded_t* client,
                             db_c_record_callback_t record_callback,
                             void* record_user_data);

int db_c_live_threaded_start_with_metadata(db_c_live_threaded_t* client,
                                           db_c_metadata_callback_t metadata_callback,
                                           void* metadata_user_data,
                                           db_c_record_callback_t record_callback,
                                           void* record_user_data);

int db_c_live_threaded_start_with_exceptions(db_c_live_threaded_t* client,
                                             db_c_metadata_callback_t metadata_callback,
                                             void* metadata_user_data,
                                             db_c_record_callback_t record_callback,
                                             void* record_user_data,
                                             db_c_exception_callback_t exception_callback,
                                             void* exception_user_data);

int db_c_live_threaded_reconnect(db_c_live_threaded_t* client);
int db_c_live_threaded_resubscribe(db_c_live_threaded_t* client);
int db_c_live_threaded_block_for_stop(db_c_live_threaded_t* client);
int db_c_live_threaded_block_for_stop_with_timeout(db_c_live_threaded_t* client,
                                                   uint64_t timeout_millis,
                                                   db_c_keep_going_t* result);

#ifdef __cplusplus
}
#endif

#endif  // DATABENTO_C_API_H
