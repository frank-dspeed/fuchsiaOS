// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_TERMINA_GUEST_MANAGER_LOG_COLLECTOR_H_
#define SRC_VIRTUALIZATION_BIN_TERMINA_GUEST_MANAGER_LOG_COLLECTOR_H_

#include "src/virtualization/third_party/vm_tools/vm_host.grpc.pb.h"

namespace termina_guest_manager {

class LogCollector : public vm_tools::LogCollector::Service {
 private:
  // |vm_tools::LogCollector::Service|
  grpc::Status CollectKernelLogs(grpc::ServerContext* context,
                                 const ::vm_tools::LogRequest* request,
                                 vm_tools::EmptyMessage* response) override;
  grpc::Status CollectUserLogs(grpc::ServerContext* context, const ::vm_tools::LogRequest* request,
                               vm_tools::EmptyMessage* response) override;

  grpc::Status CollectLogs(const vm_tools::LogRequest* request);
};

}  // namespace termina_guest_manager

#endif  // SRC_VIRTUALIZATION_BIN_TERMINA_GUEST_MANAGER_LOG_COLLECTOR_H_
