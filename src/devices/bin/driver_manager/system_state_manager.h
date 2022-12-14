// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_SYSTEM_STATE_MANAGER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_SYSTEM_STATE_MANAGER_H_

#include <fidl/fuchsia.device.manager/cpp/wire.h>
#include <lib/zx/channel.h>

class Coordinator;

class SystemStateManager : public fidl::WireServer<fuchsia_device_manager::SystemStateTransition> {
 public:
  explicit SystemStateManager(Coordinator* dev_coord) : dev_coord_(dev_coord) {}

  zx_status_t BindPowerManagerInstance(
      async_dispatcher_t* dispatcher,
      fidl::ServerEnd<fuchsia_device_manager::SystemStateTransition> server);

  // SystemStateTransition interface
  void SetTerminationSystemState(SetTerminationSystemStateRequestView request,
                                 SetTerminationSystemStateCompleter::Sync& completer) override;

  void SetMexecZbis(SetMexecZbisRequestView request,
                    SetMexecZbisCompleter::Sync& completer) override;

 private:
  Coordinator* dev_coord_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_SYSTEM_STATE_MANAGER_H_
