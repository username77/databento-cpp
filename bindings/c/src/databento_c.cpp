#include "databento/c_api.h"

#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "databento/dbn.hpp"
#include "databento/datetime.hpp"
#include "databento/enums.hpp"
#include "databento/live.hpp"
#include "databento/live_threaded.hpp"
#include "databento/record.hpp"
#include "databento/timeseries.hpp"

namespace {
thread_local std::string g_last_error;

int SetError(const char* message) {
  g_last_error = message ? message : "unknown error";
  return -1;
}

void ClearError() { g_last_error.clear(); }

std::vector<std::string> CopyStrings(const char* const* strings, std::size_t count) {
  std::vector<std::string> result;
  result.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const char* value = strings ? strings[i] : nullptr;
    if (value == nullptr) {
      throw std::invalid_argument("symbol list contains a null entry");
    }
    result.emplace_back(value);
  }
  return result;
}

db_c_record_header_t ToCHeader(const databento::RecordHeader& header) {
  db_c_record_header_t converted{};
  converted.length_words = header.length;
  converted.rtype = static_cast<std::uint8_t>(header.rtype);
  converted.publisher_id = header.publisher_id;
  converted.instrument_id = header.instrument_id;
  converted.ts_event = header.ts_event.time_since_epoch().count();
  return converted;
}

databento::Schema ToSchema(db_c_schema_t schema) {
  return static_cast<databento::Schema>(schema);
}

databento::SType ToSType(db_c_stype_t stype) {
  return static_cast<databento::SType>(stype);
}

databento::Dataset ToDataset(db_c_dataset_t dataset) {
  return static_cast<databento::Dataset>(dataset);
}

databento::VersionUpgradePolicy ToUpgradePolicy(db_c_upgrade_policy_t policy) {
  switch (policy) {
    case DB_C_UPGRADE_AS_IS:
      return databento::VersionUpgradePolicy::AsIs;
    case DB_C_UPGRADE_TO_V2:
      return databento::VersionUpgradePolicy::UpgradeToV2;
    case DB_C_UPGRADE_TO_V3:
    default:
      return databento::VersionUpgradePolicy::UpgradeToV3;
  }
}

databento::KeepGoing ToKeepGoing(db_c_keep_going_t keep_going) {
  return keep_going == DB_C_KEEP_GOING_STOP ? databento::KeepGoing::Stop
                                            : databento::KeepGoing::Continue;
}

db_c_keep_going_t FromKeepGoing(databento::KeepGoing keep_going) {
  return keep_going == databento::KeepGoing::Stop ? DB_C_KEEP_GOING_STOP
                                                  : DB_C_KEEP_GOING_CONTINUE;
}

using RecordCallbackFn = databento::RecordCallback;

RecordCallbackFn MakeRecordCallback(db_c_record_callback_t callback,
                                    void* user_data) {
  return [callback, user_data](const databento::Record& record) {
    const auto header = ToCHeader(record.Header());
    const std::size_t total_size = record.Size();
    const auto header_size = sizeof(databento::RecordHeader);
    const auto* header_bytes = reinterpret_cast<const std::byte*>(&record.Header());
    const void* body = header_bytes + header_size;
    const std::size_t body_size = total_size > header_size ? total_size - header_size : 0U;
    const auto keep_going = callback(user_data, &header, body, body_size);
    return ToKeepGoing(keep_going);
  };
}

using MetadataCallbackFn = databento::MetadataCallback;

MetadataCallbackFn MakeMetadataCallback(db_c_metadata_callback_t callback,
                                        void* user_data) {
  return [callback, user_data](databento::Metadata&& metadata) {
    if (!callback) {
      return;
    }
    const std::string metadata_text = databento::ToString(metadata);
    callback(user_data, metadata_text.c_str());
  };
}

using ExceptionCallbackFn = databento::LiveThreaded::ExceptionCallback;

ExceptionCallbackFn MakeExceptionCallback(db_c_exception_callback_t callback,
                                          void* user_data) {
  return [callback, user_data](const std::exception& exc) {
    if (!callback) {
      return databento::LiveThreaded::ExceptionAction::Stop;
    }
    const db_c_exception_action_t action = callback(user_data, exc.what());
    return action == DB_C_EXCEPTION_ACTION_RESTART
               ? databento::LiveThreaded::ExceptionAction::Restart
               : databento::LiveThreaded::ExceptionAction::Stop;
  };
}

struct db_c_live_builder {
  databento::LiveBuilder builder;
};

struct db_c_live_threaded {
  std::unique_ptr<databento::LiveThreaded> client;
};

}  // namespace

extern "C" {

const char* db_c_last_error(void) {
  return g_last_error.empty() ? nullptr : g_last_error.c_str();
}

db_c_live_builder_t* db_c_live_builder_new(void) {
  try {
    ClearError();
    auto* wrapper = new db_c_live_builder{};
    return wrapper;
  } catch (const std::exception& ex) {
    g_last_error = ex.what();
  } catch (...) {
    g_last_error = "failed to allocate live builder";
  }
  return nullptr;
}

void db_c_live_builder_free(db_c_live_builder_t* builder) {
  delete builder;
}

int db_c_live_builder_set_key(db_c_live_builder_t* builder, const char* api_key) {
  if (!builder) {
    return SetError("builder pointer is null");
  }
  if (!api_key) {
    return SetError("api_key pointer is null");
  }
  try {
    ClearError();
    builder->builder.SetKey(std::string{api_key});
    return 0;
  } catch (const std::exception& ex) {
    return SetError(ex.what());
  } catch (...) {
    return SetError("unexpected error in db_c_live_builder_set_key");
  }
}

int db_c_live_builder_set_key_from_env(db_c_live_builder_t* builder) {
  if (!builder) {
    return SetError("builder pointer is null");
  }
  try {
    ClearError();
    builder->builder.SetKeyFromEnv();
    return 0;
  } catch (const std::exception& ex) {
    return SetError(ex.what());
  } catch (...) {
    return SetError("unexpected error in db_c_live_builder_set_key_from_env");
  }
}

int db_c_live_builder_set_dataset(db_c_live_builder_t* builder, const char* dataset_code) {
  if (!builder) {
    return SetError("builder pointer is null");
  }
  if (!dataset_code) {
    return SetError("dataset_code pointer is null");
  }
  try {
    ClearError();
    builder->builder.SetDataset(std::string{dataset_code});
    return 0;
  } catch (const std::exception& ex) {
    return SetError(ex.what());
  } catch (...) {
    return SetError("unexpected error in db_c_live_builder_set_dataset");
  }
}

int db_c_live_builder_set_dataset_enum(db_c_live_builder_t* builder,
                                       db_c_dataset_t dataset) {
  if (!builder) {
    return SetError("builder pointer is null");
  }
  try {
    ClearError();
    builder->builder.SetDataset(ToDataset(dataset));
    return 0;
  } catch (const std::exception& ex) {
    return SetError(ex.what());
  } catch (...) {
    return SetError("unexpected error in db_c_live_builder_set_dataset_enum");
  }
}

int db_c_live_builder_set_send_ts_out(db_c_live_builder_t* builder, int send_ts_out) {
  if (!builder) {
    return SetError("builder pointer is null");
  }
  try {
    ClearError();
    builder->builder.SetSendTsOut(send_ts_out != 0);
    return 0;
  } catch (const std::exception& ex) {
    return SetError(ex.what());
  } catch (...) {
    return SetError("unexpected error in db_c_live_builder_set_send_ts_out");
  }
}

int db_c_live_builder_set_upgrade_policy(db_c_live_builder_t* builder,
                                         db_c_upgrade_policy_t policy) {
  if (!builder) {
    return SetError("builder pointer is null");
  }
  try {
    ClearError();
    builder->builder.SetUpgradePolicy(ToUpgradePolicy(policy));
    return 0;
  } catch (const std::exception& ex) {
    return SetError(ex.what());
  } catch (...) {
    return SetError("unexpected error in db_c_live_builder_set_upgrade_policy");
  }
}

int db_c_live_builder_set_heartbeat_interval(db_c_live_builder_t* builder, uint32_t seconds) {
  if (!builder) {
    return SetError("builder pointer is null");
  }
  try {
    ClearError();
    builder->builder.SetHeartbeatInterval(std::chrono::seconds{seconds});
    return 0;
  } catch (const std::exception& ex) {
    return SetError(ex.what());
  } catch (...) {
    return SetError("unexpected error in db_c_live_builder_set_heartbeat_interval");
  }
}

int db_c_live_builder_set_address(db_c_live_builder_t* builder, const char* gateway,
                                  uint16_t port) {
  if (!builder) {
    return SetError("builder pointer is null");
  }
  if (!gateway) {
    return SetError("gateway pointer is null");
  }
  try {
    ClearError();
    builder->builder.SetAddress(std::string{gateway}, port);
    return 0;
  } catch (const std::exception& ex) {
    return SetError(ex.what());
  } catch (...) {
    return SetError("unexpected error in db_c_live_builder_set_address");
  }
}

int db_c_live_builder_set_buffer_size(db_c_live_builder_t* builder, size_t size) {
  if (!builder) {
    return SetError("builder pointer is null");
  }
  try {
    ClearError();
    builder->builder.SetBufferSize(size);
    return 0;
  } catch (const std::exception& ex) {
    return SetError(ex.what());
  } catch (...) {
    return SetError("unexpected error in db_c_live_builder_set_buffer_size");
  }
}

db_c_live_threaded_t* db_c_live_builder_build_threaded(db_c_live_builder_t* builder) {
  if (!builder) {
    SetError("builder pointer is null");
    return nullptr;
  }
  try {
    ClearError();
    auto native_client = builder->builder.BuildThreaded();
    auto wrapper = std::make_unique<db_c_live_threaded>();
    wrapper->client = std::make_unique<databento::LiveThreaded>(std::move(native_client));
    return wrapper.release();
  } catch (const std::exception& ex) {
    SetError(ex.what());
  } catch (...) {
    SetError("unexpected error in db_c_live_builder_build_threaded");
  }
  return nullptr;
}

void db_c_live_threaded_free(db_c_live_threaded_t* client) {
  delete client;
}

int db_c_live_threaded_subscribe(db_c_live_threaded_t* client, const char* const* symbols,
                                 size_t symbol_count, db_c_schema_t schema,
                                 db_c_stype_t stype_in) {
  if (!client || !client->client) {
    return SetError("client pointer is null");
  }
  try {
    ClearError();
    auto symbol_vec = CopyStrings(symbols, symbol_count);
    client->client->Subscribe(symbol_vec, ToSchema(schema), ToSType(stype_in));
    return 0;
  } catch (const std::exception& ex) {
    return SetError(ex.what());
  } catch (...) {
    return SetError("unexpected error in db_c_live_threaded_subscribe");
  }
}

int db_c_live_threaded_subscribe_from_unix(db_c_live_threaded_t* client,
                                           const char* const* symbols,
                                           size_t symbol_count, db_c_schema_t schema,
                                           db_c_stype_t stype_in, uint64_t start_unix_nanos) {
  if (!client || !client->client) {
    return SetError("client pointer is null");
  }
  try {
    ClearError();
    auto symbol_vec = CopyStrings(symbols, symbol_count);
    databento::UnixNanos start{databento::UnixNanos::duration{start_unix_nanos}};
    client->client->Subscribe(symbol_vec, ToSchema(schema), ToSType(stype_in), start);
    return 0;
  } catch (const std::exception& ex) {
    return SetError(ex.what());
  } catch (...) {
    return SetError("unexpected error in db_c_live_threaded_subscribe_from_unix");
  }
}

int db_c_live_threaded_subscribe_from_str(db_c_live_threaded_t* client,
                                          const char* const* symbols,
                                          size_t symbol_count, db_c_schema_t schema,
                                          db_c_stype_t stype_in, const char* start) {
  if (!client || !client->client) {
    return SetError("client pointer is null");
  }
  if (!start) {
    return SetError("start pointer is null");
  }
  try {
    ClearError();
    auto symbol_vec = CopyStrings(symbols, symbol_count);
    client->client->Subscribe(symbol_vec, ToSchema(schema), ToSType(stype_in),
                              std::string{start});
    return 0;
  } catch (const std::exception& ex) {
    return SetError(ex.what());
  } catch (...) {
    return SetError("unexpected error in db_c_live_threaded_subscribe_from_str");
  }
}

int db_c_live_threaded_subscribe_with_snapshot(db_c_live_threaded_t* client,
                                               const char* const* symbols,
                                               size_t symbol_count, db_c_schema_t schema,
                                               db_c_stype_t stype_in) {
  if (!client || !client->client) {
    return SetError("client pointer is null");
  }
  try {
    ClearError();
    auto symbol_vec = CopyStrings(symbols, symbol_count);
    client->client->SubscribeWithSnapshot(symbol_vec, ToSchema(schema), ToSType(stype_in));
    return 0;
  } catch (const std::exception& ex) {
    return SetError(ex.what());
  } catch (...) {
    return SetError("unexpected error in db_c_live_threaded_subscribe_with_snapshot");
  }
}

int db_c_live_threaded_start(db_c_live_threaded_t* client,
                             db_c_record_callback_t record_callback,
                             void* record_user_data) {
  if (!client || !client->client) {
    return SetError("client pointer is null");
  }
  if (!record_callback) {
    return SetError("record_callback pointer is null");
  }
  try {
    ClearError();
    auto cb = MakeRecordCallback(record_callback, record_user_data);
    client->client->Start(std::move(cb));
    return 0;
  } catch (const std::exception& ex) {
    return SetError(ex.what());
  } catch (...) {
    return SetError("unexpected error in db_c_live_threaded_start");
  }
}

int db_c_live_threaded_start_with_metadata(db_c_live_threaded_t* client,
                                           db_c_metadata_callback_t metadata_callback,
                                           void* metadata_user_data,
                                           db_c_record_callback_t record_callback,
                                           void* record_user_data) {
  if (!client || !client->client) {
    return SetError("client pointer is null");
  }
  if (!record_callback) {
    return SetError("record_callback pointer is null");
  }
  try {
    ClearError();
    auto record_cb = MakeRecordCallback(record_callback, record_user_data);
    auto metadata_cb = MakeMetadataCallback(metadata_callback, metadata_user_data);
    client->client->Start(std::move(metadata_cb), std::move(record_cb));
    return 0;
  } catch (const std::exception& ex) {
    return SetError(ex.what());
  } catch (...) {
    return SetError("unexpected error in db_c_live_threaded_start_with_metadata");
  }
}

int db_c_live_threaded_start_with_exceptions(db_c_live_threaded_t* client,
                                             db_c_metadata_callback_t metadata_callback,
                                             void* metadata_user_data,
                                             db_c_record_callback_t record_callback,
                                             void* record_user_data,
                                             db_c_exception_callback_t exception_callback,
                                             void* exception_user_data) {
  if (!client || !client->client) {
    return SetError("client pointer is null");
  }
  if (!record_callback) {
    return SetError("record_callback pointer is null");
  }
  try {
    ClearError();
    auto record_cb = MakeRecordCallback(record_callback, record_user_data);
    auto metadata_cb = MakeMetadataCallback(metadata_callback, metadata_user_data);
    auto exception_cb = MakeExceptionCallback(exception_callback, exception_user_data);
    client->client->Start(std::move(metadata_cb), std::move(record_cb),
                          std::move(exception_cb));
    return 0;
  } catch (const std::exception& ex) {
    return SetError(ex.what());
  } catch (...) {
    return SetError("unexpected error in db_c_live_threaded_start_with_exceptions");
  }
}

int db_c_live_threaded_reconnect(db_c_live_threaded_t* client) {
  if (!client || !client->client) {
    return SetError("client pointer is null");
  }
  try {
    ClearError();
    client->client->Reconnect();
    return 0;
  } catch (const std::exception& ex) {
    return SetError(ex.what());
  } catch (...) {
    return SetError("unexpected error in db_c_live_threaded_reconnect");
  }
}

int db_c_live_threaded_resubscribe(db_c_live_threaded_t* client) {
  if (!client || !client->client) {
    return SetError("client pointer is null");
  }
  try {
    ClearError();
    client->client->Resubscribe();
    return 0;
  } catch (const std::exception& ex) {
    return SetError(ex.what());
  } catch (...) {
    return SetError("unexpected error in db_c_live_threaded_resubscribe");
  }
}

int db_c_live_threaded_block_for_stop(db_c_live_threaded_t* client) {
  if (!client || !client->client) {
    return SetError("client pointer is null");
  }
  try {
    ClearError();
    client->client->BlockForStop();
    return 0;
  } catch (const std::exception& ex) {
    return SetError(ex.what());
  } catch (...) {
    return SetError("unexpected error in db_c_live_threaded_block_for_stop");
  }
}

int db_c_live_threaded_block_for_stop_with_timeout(db_c_live_threaded_t* client,
                                                   uint64_t timeout_millis,
                                                   db_c_keep_going_t* result) {
  if (!client || !client->client) {
    return SetError("client pointer is null");
  }
  if (!result) {
    return SetError("result pointer is null");
  }
  try {
    ClearError();
    const auto keep_going =
        client->client->BlockForStop(std::chrono::milliseconds{timeout_millis});
    *result = FromKeepGoing(keep_going);
    return 0;
  } catch (const std::exception& ex) {
    return SetError(ex.what());
  } catch (...) {
    return SetError("unexpected error in db_c_live_threaded_block_for_stop_with_timeout");
  }
}

}  // extern "C"
