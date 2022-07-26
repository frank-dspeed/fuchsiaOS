// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/standalone-test/standalone.h>
#include <lib/stdcompat/array.h>
#include <lib/zx/channel.h>
#include <lib/zx/eventpair.h>
#include <zircon/errors.h>
#include <zircon/rights.h>
#include <zircon/sanitizer.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <cstdint>
#include <string_view>

#include <zxtest/zxtest.h>

#include "helper.h"

namespace {

class SvcSingleProcessTest : public zxtest::Test {
 public:
  void SetUp() final {
    static zx::channel svc_stash;
    static zx::channel svc_server;

    // Prevents multiple iterations from discarding this.
    if (!svc_stash) {
      svc_stash = GetSvcStash();
      ASSERT_TRUE(svc_stash);
    }

    // Order matters, provider is stored first.
    if (!svc_server) {
      ASSERT_NO_FATAL_FAILURE(GetStashedSvc(svc_stash.borrow(), svc_server));
    }

    svc_stash_ = svc_stash.borrow();
    svc_ = standalone::GetNsDir("/svc");
    stashed_svc_ = svc_server.borrow();
  }

  zx::unowned_channel svc_stash() { return svc_stash_->borrow(); }
  zx::unowned_channel local_svc() { return svc_->borrow(); }
  zx::unowned_channel stashed_svc() { return stashed_svc_->borrow(); }

 private:
  zx::unowned_channel svc_stash_;
  zx::unowned_channel svc_;
  zx::unowned_channel stashed_svc_;
};

TEST_F(SvcSingleProcessTest, SvcStubIsValidHandle) { ASSERT_TRUE(local_svc()->is_valid()); }

TEST_F(SvcSingleProcessTest, SvcStashIsValidHandle) {
  ASSERT_TRUE(svc_stash()->is_valid());
  ASSERT_TRUE(stashed_svc()->is_valid());
}

TEST_F(SvcSingleProcessTest, WritingIntoSvcShowsUpInStashHandle) {
  ASSERT_TRUE(local_svc()->is_valid());
  ASSERT_TRUE(stashed_svc()->is_valid());

  auto written_message = cpp20::to_array("Hello World!");
  ASSERT_OK(local_svc()->write(0, written_message.data(), written_message.size(), nullptr, 0));

  decltype(written_message) read_message = {};
  uint32_t actual_bytes, actual_handles;
  ASSERT_OK(stashed_svc()->read(0, read_message.data(), nullptr, read_message.size(), 0,
                                &actual_bytes, &actual_handles));

  EXPECT_EQ(read_message, written_message);
}

TEST_F(SvcSingleProcessTest, SanitizerPublishDataShowsUpInStashedHandle) {
  ASSERT_TRUE(local_svc()->is_valid());
  ASSERT_TRUE(stashed_svc()->is_valid());

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(124, 0, &vmo));

  // Channel transitions from not readable to readable.
  zx_signals_t observed = 0;
  ASSERT_STATUS(stashed_svc()->wait_one(ZX_CHANNEL_READABLE, zx::time::infinite_past(), &observed),
                ZX_ERR_TIMED_OUT);

  constexpr const char* kDataSink = "some_sink_name";
  zx_koid_t vmo_koid = GetKoid(vmo.get());

  zx::eventpair token_1(__sanitizer_publish_data(kDataSink, vmo.release()));

  zx_koid_t token_koid = GetPeerKoid(token_1.get());
  ASSERT_NE(token_koid, ZX_KOID_INVALID);
  ASSERT_NE(vmo_koid, ZX_KOID_INVALID);

  observed = 0;
  ASSERT_OK(stashed_svc()->wait_one(ZX_CHANNEL_READABLE, zx::time::infinite_past(), &observed));
  ASSERT_TRUE((observed & ZX_CHANNEL_READABLE) != 0);

  // There should be an open request after this.
  Message debug_msg;
  ASSERT_NO_FAILURES(GetDebugDataMessage(stashed_svc(), debug_msg));
  DebugDataMessageView view(debug_msg);
  ASSERT_EQ(view.sink(), kDataSink);
  ASSERT_EQ(GetKoid(view.vmo()->get()), vmo_koid);
  ASSERT_EQ(GetKoid(view.token()->get()), token_koid);
}

}  // namespace
