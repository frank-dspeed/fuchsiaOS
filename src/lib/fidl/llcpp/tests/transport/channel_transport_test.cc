// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/transaction_header.h>
#include <lib/fidl/cpp/wire/internal/transport_channel.h>

#include <atomic>

#include <zxtest/zxtest.h>

TEST(ChannelTransport, Success) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  zx::channel ch1, ch2;
  ASSERT_OK(zx::channel::create(0, &ch1, &ch2));
  fidl_message_header_t message;
  fidl::InitTxnHeader(&message, 123, 456, fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_OK(ch2.write(0, &message, sizeof(message), nullptr, 0));

  bool success = false;
  fidl::internal::ChannelWaiter waiter(
      ch1.get(), dispatcher,
      [&success](fidl::IncomingHeaderAndMessage&,
                 const fidl::internal::MessageStorageViewBase* storage_view) { success = true; },
      [](fidl::UnbindInfo) { ZX_PANIC("shouldn't get here"); });
  ASSERT_OK(waiter.Begin());

  loop.RunUntilIdle();
  ASSERT_TRUE(success);
}

TEST(ChannelTransport, Failure) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  zx::channel ch1, ch2;
  ASSERT_OK(zx::channel::create(0, &ch1, &ch2));
  ch2.reset();

  std::optional<fidl::UnbindInfo> failure;
  fidl::internal::ChannelWaiter waiter(
      ch1.get(), dispatcher,
      [](fidl::IncomingHeaderAndMessage&, fidl::internal::MessageStorageViewBase*) {
        ZX_PANIC("shouldn't get here");
      },
      [&failure](fidl::UnbindInfo info) { failure = info; });
  ASSERT_OK(waiter.Begin());

  loop.RunUntilIdle();
  ASSERT_TRUE(failure.has_value());
  ASSERT_EQ(ZX_ERR_PEER_CLOSED, failure->status());
}
