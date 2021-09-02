// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_APP_COBALT_APP_H_
#define SRC_COBALT_BIN_APP_COBALT_APP_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/metrics/cpp/fidl.h>
#include <fuchsia/process/lifecycle/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/inspect/cpp/inspect.h>
#include <stdlib.h>

#include <chrono>
#include <fstream>
#include <memory>
#include <string>

#include "lib/fidl/cpp/binding_set.h"
#include "lib/sys/cpp/component_context.h"
#include "src/cobalt/bin/app/activity_listener_impl.h"
#include "src/cobalt/bin/app/cobalt_controller_impl.h"
#include "src/cobalt/bin/app/configuration_data.h"
#include "src/cobalt/bin/app/diagnostics_impl.h"
#include "src/cobalt/bin/app/logger_factory_impl.h"
#include "src/cobalt/bin/app/process_lifecycle_impl.h"
#include "src/cobalt/bin/app/system_data_updater_impl.h"
#include "src/cobalt/bin/app/timer_manager.h"
#include "src/cobalt/bin/app/user_consent_watcher.h"
#include "src/cobalt/bin/utils/clock.h"
#include "src/cobalt/bin/utils/fuchsia_http_client.h"
#include "third_party/cobalt/src/public/cobalt_config.h"
#include "third_party/cobalt/src/public/cobalt_service_interface.h"

namespace cobalt {

// Main app, which manages all of Cobalt's functionality.
//
// To test run:
//    fx set --with-base //bundles:tools,//src/cobalt/bin:cobalt_tests;
//    fx test cobalt_testapp_no_network
class CobaltApp {
 public:
  // |dispatcher| The async_t to be used for all asynchronous operations.
  //
  // |upload_schedule_cfg| Defines when the shipping_manager should upload
  //                       observations.
  //
  // |event_aggregator_backfill_days| The number of past days, in addition to the previous day,
  //                                  for which local aggregation generates observations. If  a
  //                                  device is unable to generate observations for more than this
  //                                  number of days, we may lose older aggregated data.
  //
  // |start_event_aggregator_worker| If true, starts the EventAggregatorManager's worker thread
  //                                 after constructing the EventAggregatorManager.
  //
  // |use_memory_observation_store| If this is true, the observation stores will be in-memory
  //                                only, otherwise they will be file-system backed.
  //
  // |max_bytes_per_observation_store| The maximum number of bytes to store for each of the
  //                                   observation_stores.
  //
  // |product_name| A product name included in the SystemProfile that is implicitly part of every
  //                Cobalt metric.
  //
  //                Example: products/core.gni
  //
  // |board_name| A board name that may be included in the SystemProfile that is implicitly part
  //              of every Cobalt metric.
  //
  //              Examples: astro, vim2, qemu
  //
  // |version| The version of the running system included in the SystemProfile that is implicitly
  //           part of every Cobalt metric.
  //
  //           Example: 20190220_01_RC00
  static CobaltApp CreateCobaltApp(
      std::unique_ptr<sys::ComponentContext> context, async_dispatcher_t* dispatcher,
      inspect::Node inspect_node, UploadScheduleConfig upload_schedule_cfg,
      size_t event_aggregator_backfill_days, bool start_event_aggregator_worker,
      bool use_memory_observation_store, size_t max_bytes_per_observation_store,
      const std::string& product_name, const std::string& board_name, const std::string& version);

 private:
  friend class CobaltAppTest;
  friend class CreateCobaltConfigTest;

  static CobaltConfig CreateCobaltConfig(
      async_dispatcher_t* dispatcher, const std::string& global_metrics_registry_path,
      const FuchsiaConfigurationData& configuration_data,
      FuchsiaSystemClockInterface* validated_clock,
      utils::FuchsiaHTTPClient::LoaderFactory http_loader_factory,
      UploadScheduleConfig upload_schedule_cfg, size_t event_aggregator_backfill_days,
      bool use_memory_observation_store, size_t max_bytes_per_observation_store,
      const std::string& product_name, const std::string& board_name, const std::string& version,
      std::unique_ptr<ActivityListenerImpl> listener, std::unique_ptr<DiagnosticsImpl> diagnostics);

  CobaltApp(std::unique_ptr<sys::ComponentContext> context, async_dispatcher_t* dispatcher,
            inspect::Node inspect_node, inspect::Node inspect_config_node,
            inspect::ValueList inspect_values,
            std::unique_ptr<CobaltServiceInterface> cobalt_service,
            std::unique_ptr<FuchsiaSystemClockInterface> validated_clock,
            bool start_event_aggregator_worker, bool watch_for_user_consent);

  static encoder::ClientSecret getClientSecret();

  std::unique_ptr<sys::ComponentContext> context_;

  inspect::Node inspect_node_;
  inspect::Node inspect_config_node_;
  inspect::ValueList inspect_values_;

  std::unique_ptr<CobaltServiceInterface> cobalt_service_;

  std::unique_ptr<FuchsiaSystemClockInterface> validated_clock_;

  TimerManager timer_manager_;

  std::unique_ptr<CobaltControllerImpl> controller_impl_;
  fidl::BindingSet<fuchsia::cobalt::Controller> controller_bindings_;

  std::unique_ptr<LoggerFactoryImpl> logger_factory_impl_;
  fidl::BindingSet<fuchsia::cobalt::LoggerFactory> logger_factory_bindings_;

  std::unique_ptr<MetricEventLoggerFactoryImpl> metric_event_logger_factory_impl_;
  fidl::BindingSet<fuchsia::metrics::MetricEventLoggerFactory>
      metric_event_logger_factory_bindings_;

  std::unique_ptr<fuchsia::cobalt::SystemDataUpdater> system_data_updater_impl_;
  fidl::BindingSet<fuchsia::cobalt::SystemDataUpdater> system_data_updater_bindings_;

  std::unique_ptr<cobalt::ProcessLifecycle> process_lifecycle_impl_;
  fidl::BindingSet<fuchsia::process::lifecycle::Lifecycle> process_lifecycle_bindings_;

  std::unique_ptr<UserConsentWatcher> user_consent_watcher_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CobaltApp);
};

std::string ReadGlobalMetricsRegistryBytes(const std::string& global_metrics_registry_path);

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_APP_COBALT_APP_H_
