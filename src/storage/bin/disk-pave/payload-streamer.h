// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BIN_DISK_PAVE_PAYLOAD_STREAMER_H_
#define SRC_STORAGE_BIN_DISK_PAVE_PAYLOAD_STREAMER_H_

#include <fuchsia/paver/llcpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>

#include <fbl/unique_fd.h>

namespace disk_pave {

class PayloadStreamer : public fuchsia_paver::PayloadStream::Interface {
 public:
  PayloadStreamer(fidl::ServerEnd<fuchsia_paver::PayloadStream> server_end, fbl::unique_fd payload);
  ~PayloadStreamer();

  PayloadStreamer(const PayloadStreamer&) = delete;
  PayloadStreamer& operator=(const PayloadStreamer&) = delete;
  PayloadStreamer(PayloadStreamer&&) = delete;
  PayloadStreamer& operator=(PayloadStreamer&&) = delete;

  void RegisterVmo(zx::vmo vmo, RegisterVmoCompleter::Sync& completer);

  void ReadData(ReadDataCompleter::Sync& completer);

 private:
  fbl::unique_fd payload_;
  zx::vmo vmo_;
  fzl::VmoMapper mapper_;
  bool eof_reached_ = false;
};

}  // namespace disk_pave

#endif  // SRC_STORAGE_BIN_DISK_PAVE_PAYLOAD_STREAMER_H_
