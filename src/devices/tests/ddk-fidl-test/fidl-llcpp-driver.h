// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTS_DDK_FIDL_TEST_FIDL_LLCPP_DRIVER_H_
#define SRC_DEVICES_TESTS_DDK_FIDL_TEST_FIDL_LLCPP_DRIVER_H_

#include <fidl/fuchsia.hardware.test/cpp/wire.h>
#include <lib/ddk/driver.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/event.h>
#include <lib/zx/socket.h>
#include <zircon/types.h>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/mutex.h>

namespace fidl {

class DdkFidlDevice;
using DeviceType =
    ddk::Device<DdkFidlDevice, ddk::Messageable<fuchsia_hardware_test::Device>::Mixin>;

class DdkFidlDevice : public DeviceType {
 public:
  explicit DdkFidlDevice(zx_device_t* parent) : DeviceType(parent) {}

  static zx_status_t Create(void* ctx, zx_device_t* dev);
  zx_status_t Bind();

  // Device protocol implementation.
  void DdkRelease();

  void GetChannel(GetChannelCompleter::Sync& completer) override;
};
}  // namespace fidl

#endif  // SRC_DEVICES_TESTS_DDK_FIDL_TEST_FIDL_LLCPP_DRIVER_H_
