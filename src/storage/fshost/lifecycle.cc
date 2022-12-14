// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lifecycle.h"

#include <lib/fidl-async/cpp/bind.h>
#include <lib/syslog/cpp/macros.h>

#include <iostream>

#include "src/sys/lib/stdout-to-debuglog/cpp/stdout-to-debuglog.h"

namespace fshost {

zx_status_t LifecycleServer::Create(async_dispatcher_t* dispatcher, FsManager* fs_manager,
                                    fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle> chan) {
  zx_status_t status = fidl::BindSingleInFlightOnly(dispatcher, std::move(chan),
                                                    std::make_unique<LifecycleServer>(fs_manager));
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "failed to bind lifecycle service";
  }
  return status;
}

void LifecycleServer::Stop(StopCompleter::Sync& completer) {
  FX_LOGS(INFO) << "received shutdown command over lifecycle interface";

  // This message is logged to debuglog as well as syslog because syslog's delivery guarantees
  // are insufficient for our tests.
  //
  // TODO(https://fxbug.dev/97630): Remove this when syslog's delivery guarantees are
  // sufficient.
  if (zx_status_t status = StdoutToDebuglog::Init(); status != ZX_OK) {
    FX_PLOGS(WARNING, status) << "failed to redirect stdout to debuglog";
  } else {
    std::cout << "[fshost] received shutdown command over lifecycle interface" << std::endl;
  }

  fs_manager_->Shutdown([completer = completer.ToAsync()](zx_status_t status) mutable {
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "filesystem shutdown failed";
    } else {
      // There are tests that watch for this message that will need updating if it changes.
      FX_LOGS(INFO) << "fshost shutdown complete";
      // This message is logged to debuglog as well as syslog because syslog's delivery guarantees
      // are insufficient for our tests.
      //
      // TODO(https://fxbug.dev/97630): Remove this when syslog's delivery guarantees are
      // sufficient.
      if (zx_status_t status = StdoutToDebuglog::Init(); status != ZX_OK) {
        FX_PLOGS(WARNING, status) << "failed to redirect stdout to debuglog";
      } else {
        std::cout << "[fshost] fshost shutdown complete" << std::endl;
      }
    }
    completer.Close(status);
  });
}

}  // namespace fshost
