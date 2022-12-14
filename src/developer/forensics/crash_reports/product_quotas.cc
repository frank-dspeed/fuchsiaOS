// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/product_quotas.h"

#include <lib/async/cpp/task.h>
#include <lib/zx/time.h>

#include <optional>
#include <string>
#include <utility>

#include "rapidjson/prettywriter.h"
#include "rapidjson/rapidjson.h"
#include "rapidjson/stringbuffer.h"
#include "src/developer/forensics/crash_reports/product.h"
#include "src/developer/forensics/utils/time.h"
#include "src/developer/forensics/utils/utc_clock_ready_watcher.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/timekeeper/clock.h"
#include "third_party/rapidjson/include/rapidjson/error/en.h"

namespace forensics {
namespace crash_reports {
namespace {

constexpr char kNextResetKey[] = "next_reset_time_utc_nanos";
constexpr char kQuotasKey[] = "quotas";
constexpr zx::duration kResetPeriod = zx::hour(24);

std::string Key(const Product& product) {
  if (product.version.HasValue()) {
    return fxl::StringPrintf("%s-%s", product.name.c_str(), product.version.Value().c_str());
  } else {
    return product.name;
  }
}

// Since UTC epoch is 00:00:00 (midnight) on 1 January 1970, using truncating division on |time|
// will give us the number of days x between UTC epoch and the previous midnight. Multiply x by the
// number of nanoseconds in a day since timekeeper::time_utc is in nanoseconds.
timekeeper::time_utc StartOfDay(const timekeeper::time_utc time) {
  return timekeeper::time_utc((time.get() / zx::hour(24).get()) * zx::hour(24).get());
}

}  // namespace

ProductQuotas::ProductQuotas(async_dispatcher_t* dispatcher, timekeeper::Clock* clock,
                             const std::optional<uint64_t> quota, std::string quota_filepath,
                             UtcClockReadyWatcherBase* utc_clock_ready_watcher)
    : dispatcher_(dispatcher),
      clock_(clock),
      quota_(quota),
      quota_filepath_(std::move(quota_filepath)),
      utc_clock_ready_watcher_(utc_clock_ready_watcher) {
  if (!quota_.has_value()) {
    FX_LOGS(INFO) << "No quota specified. Deleting " << quota_filepath_;
    files::DeletePath(quota_filepath_, /*recursive=*/true);
    return;
  }

  RestoreFromJson();

  // Assume a 24 hour reset period until UTC clock starts.
  reset_task_.PostDelayed(dispatcher_, kResetPeriod);

  // This lambda could execute immediately if the UTC clock is already ready. Registering this
  // callback with UtcClockReadyWatcherBase must be the last thing done in the constructor because
  // OnClockStart requires initialization performed earlier in the constructor.
  auto self = ptr_factory_.GetWeakPtr();
  utc_clock_ready_watcher->OnClockReady([self] {
    if (self) {
      self->OnClockStart();
    }
  });
}

bool ProductQuotas::HasQuotaRemaining(const Product& product) {
  // If no quota has been set, return true by default.
  if (!quota_.has_value()) {
    return true;
  }

  const auto key = Key(product);
  if (remaining_quotas_.find(key) == remaining_quotas_.end()) {
    remaining_quotas_[key] = quota_.value();
    UpdateJson(key, quota_.value());
  }

  return remaining_quotas_[key] != 0u;
}

void ProductQuotas::DecrementRemainingQuota(const Product& product) {
  // If no quota has been set, there's nothing to decrement.
  if (!quota_.has_value()) {
    return;
  }

  const auto key = Key(product);
  FX_CHECK(remaining_quotas_.find(key) != remaining_quotas_.end());
  FX_CHECK(remaining_quotas_[key] > 0);

  --(remaining_quotas_[key]);
  UpdateJson(key, remaining_quotas_[key]);
}

void ProductQuotas::Reset() {
  // If no quota has been set, resetting is a no-op.
  if (!quota_.has_value()) {
    return;
  }

  FX_LOGS(INFO) << "Resetting quota for all products";
  remaining_quotas_.clear();
  quota_json_.SetObject();
  files::DeletePath(quota_filepath_, /*recursive=*/true);

  if (utc_clock_ready_watcher_->IsUtcClockReady()) {
    const auto current_time = CurrentUtcTimeRaw(clock_);
    FX_CHECK(current_time.has_value());

    // Resets may not execute exactly at UTC midnight because the system's UTC clock drifts and is
    // subject to correction. The start of the next UTC day needs to be calculated from the
    // previously saved value in case Reset executes before midnight of the current day and the
    // "next" midnight is in a short period of time. For example, if quotas were to be reset at
    // 00:00 of February 2nd and Reset ran at 23:59 of February 1st, the next midnight would be
    // 00:00 February 2nd.
    next_reset_utc_time_ = StartOfDay(*next_reset_utc_time_ + zx::hour(24));
    const zx::duration time_until_next_reset = *next_reset_utc_time_ - *current_time;
    UpdateJson(*next_reset_utc_time_);
    reset_task_.PostDelayed(dispatcher_, time_until_next_reset);
  } else {
    reset_task_.PostDelayed(dispatcher_, kResetPeriod);
  }
}

void ProductQuotas::OnClockStart() {
  reset_task_.Cancel();

  const auto current_time = CurrentUtcTimeRaw(clock_);
  FX_CHECK(current_time.has_value());

  if (!next_reset_utc_time_.has_value()) {
    // Case 1: A next reset time wasn't persisted in the JSON file.
    next_reset_utc_time_ = StartOfDay(*current_time + zx::hour(24));
    const zx::duration time_until_next_reset = *next_reset_utc_time_ - *current_time;

    UpdateJson(*next_reset_utc_time_);
    reset_task_.PostDelayed(dispatcher_, time_until_next_reset);
  } else if (*current_time > *next_reset_utc_time_) {
    // Case 2: Deadline already passed. Set the "next reset" to be the previous midnight so that
    // Reset() calculates the next midnight correctly.
    next_reset_utc_time_ = StartOfDay(*current_time);
    Reset();
  } else {
    // Case 3: Deadline not yet passed.
    const zx::duration time_until_next_reset = *next_reset_utc_time_ - *current_time;
    reset_task_.PostDelayed(dispatcher_, time_until_next_reset);
  }
}

// Product "quotas" keys will be determined using the "Key" function in this file. JSON format will
// be:
// {
//    "next_reset_time_utc_nanos": *utc-time in nanoseconds*,
//    "quotas": {
//      "foo-version": *remaining quota*,
//      "bar": *remaining quota*,
//    }
// }
void ProductQuotas::UpdateJson(const std::string& key, uint64_t remaining_quota) {
  auto& allocator = quota_json_.GetAllocator();

  if (!quota_json_.HasMember(kQuotasKey)) {
    quota_json_.AddMember(kQuotasKey, rapidjson::Value(rapidjson::kObjectType), allocator);
  }

  const auto& json_quotas = quota_json_[kQuotasKey].GetObject();

  if (!json_quotas.HasMember(key)) {
    json_quotas.AddMember(rapidjson::Value(key, allocator), rapidjson::Value(0u), allocator);
  }

  json_quotas[key] = remaining_quota;

  WriteJson();
}

void ProductQuotas::UpdateJson(timekeeper::time_utc next_reset_utc_time) {
  auto& allocator = quota_json_.GetAllocator();

  if (!quota_json_.HasMember(kNextResetKey)) {
    quota_json_.AddMember(rapidjson::Value(kNextResetKey, allocator), rapidjson::Value(0),
                          allocator);
  }

  quota_json_[kNextResetKey] = next_reset_utc_time.get();

  WriteJson();
}

void ProductQuotas::WriteJson() {
  rapidjson::StringBuffer buffer;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);

  quota_json_.Accept(writer);

  if (!files::WriteFile(quota_filepath_, buffer.GetString(), buffer.GetLength())) {
    FX_LOGS(ERROR) << "Failed to write remaining quota contents to " << quota_filepath_;
  }
}

void ProductQuotas::RestoreFromJson() {
  quota_json_.SetObject();

  // If the file doesn't exit, return.
  if (!files::IsFile(quota_filepath_)) {
    return;
  }

  // Check-fail if the file can't be read.
  std::string json;
  FX_CHECK(files::ReadFileToString(quota_filepath_, &json));

  if (const rapidjson::ParseResult ok = quota_json_.Parse(json); !ok) {
    FX_LOGS(ERROR) << "Error parsing product quotas as JSON at offset " << ok.Offset() << " "
                   << GetParseError_En(ok.Code());
    files::DeletePath(quota_filepath_, /*recursive=*/true);
    return;
  }

  if (quota_json_.HasMember(kNextResetKey) && quota_json_[kNextResetKey].IsInt64()) {
    next_reset_utc_time_ = timekeeper::time_utc(quota_json_[kNextResetKey].GetInt64());
  }

  // Each product in the json is represented by an object containing string-int pairs
  // that are the remaining quota for each product.
  if (quota_json_.HasMember(kQuotasKey) && quota_json_[kQuotasKey].IsObject()) {
    const auto& json_quotas = quota_json_[kQuotasKey].GetObject();
    for (const auto& member : json_quotas) {
      if (member.name.IsString() && member.value.IsInt64()) {
        const std::string product_key = member.name.GetString();
        const uint64_t remaining_quota = member.value.GetUint64();
        remaining_quotas_[product_key] = remaining_quota;
      }
    }
  }
}

}  // namespace crash_reports
}  // namespace forensics
