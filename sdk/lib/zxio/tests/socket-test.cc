// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.posix.socket.packet/cpp/wire_test_base.h>
#include <fidl/fuchsia.posix.socket.raw/cpp/wire_test_base.h>
#include <fidl/fuchsia.posix.socket/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/socket.h>
#include <lib/zxio/cpp/create_with_type.h>
#include <lib/zxio/zxio.h>
#include <zircon/types.h>

#include <zxtest/zxtest.h>

#include "sdk/lib/zxio/private.h"
#include "sdk/lib/zxio/tests/test_socket_server.h"

namespace {

class SynchronousDatagramSocketTest : public zxtest::Test {
 public:
  void SetUp() final {
    ASSERT_OK(zx::eventpair::create(0u, &event0_, &event1_));

    zx::status node_server = fidl::CreateEndpoints(&client_end_);
    ASSERT_OK(node_server.status_value());

    fidl::BindServer(control_loop_.dispatcher(), std::move(*node_server), &server_);
    control_loop_.StartThread("control");
  }

  void Init() {
    ASSERT_OK(zxio_synchronous_datagram_socket_init(&storage_, TakeEvent(), TakeClientEnd()));
    zxio_ = &storage_.io;
  }

  void TearDown() final {
    if (zxio_) {
      ASSERT_OK(zxio_close(zxio_));
    }
    control_loop_.Shutdown();
  }

  zx::eventpair TakeEvent() { return std::move(event0_); }
  fidl::ClientEnd<fuchsia_posix_socket::SynchronousDatagramSocket> TakeClientEnd() {
    return std::move(client_end_);
  }
  zxio_storage_t* storage() { return &storage_; }
  zxio_t* zxio() { return zxio_; }

 private:
  zxio_storage_t storage_;
  zxio_t* zxio_{nullptr};
  zx::eventpair event0_, event1_;
  fidl::ClientEnd<fuchsia_posix_socket::SynchronousDatagramSocket> client_end_;
  zxio_tests::SynchronousDatagramSocketServer server_;
  async::Loop control_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

}  // namespace

TEST_F(SynchronousDatagramSocketTest, Basic) { Init(); }

TEST_F(SynchronousDatagramSocketTest, Release) {
  Init();

  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_release(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);

  EXPECT_OK(zx_handle_close(handle));
}

TEST_F(SynchronousDatagramSocketTest, Borrow) {
  Init();

  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_borrow(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);
}

TEST_F(SynchronousDatagramSocketTest, CreateWithType) {
  ASSERT_OK(zxio_create_with_type(storage(), ZXIO_OBJECT_TYPE_SYNCHRONOUS_DATAGRAM_SOCKET,
                                  TakeEvent().release(), TakeClientEnd().TakeChannel().release()));
  ASSERT_OK(zxio_close(&storage()->io));
}

namespace {

class StreamSocketTest : public zxtest::Test {
 public:
  void SetUp() final {
    ASSERT_OK(zx::socket::create(ZX_SOCKET_STREAM, &socket_, &peer_));
    ASSERT_OK(socket_.get_info(ZX_INFO_SOCKET, &info_, sizeof(info_), nullptr, nullptr));

    zx::status server_end = fidl::CreateEndpoints(&client_end_);
    ASSERT_OK(server_end.status_value());

    fidl::BindServer(control_loop_.dispatcher(), std::move(*server_end), &server_);
    control_loop_.StartThread("control");
  }

  void Init() {
    ASSERT_OK(zxio_stream_socket_init(&storage_, TakeSocket(), info(), /*is_connected=*/false,
                                      TakeClientEnd()));
    zxio_ = &storage_.io;
  }

  void TearDown() final {
    if (zxio_) {
      ASSERT_OK(zxio_close(zxio_));
    }
    control_loop_.Shutdown();
  }

  zx_info_socket_t& info() { return info_; }
  zx::socket TakeSocket() { return std::move(socket_); }
  fidl::ClientEnd<fuchsia_posix_socket::StreamSocket> TakeClientEnd() {
    return std::move(client_end_);
  }
  zxio_storage_t* storage() { return &storage_; }
  zxio_t* zxio() { return zxio_; }

 private:
  zxio_storage_t storage_;
  zxio_t* zxio_{nullptr};
  zx_info_socket_t info_;
  zx::socket socket_, peer_;
  fidl::ClientEnd<fuchsia_posix_socket::StreamSocket> client_end_;
  zxio_tests::StreamSocketServer server_;
  async::Loop control_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

}  // namespace

TEST_F(StreamSocketTest, Basic) { Init(); }

TEST_F(StreamSocketTest, Release) {
  Init();

  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_release(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);

  EXPECT_OK(zx_handle_close(handle));
}

TEST_F(StreamSocketTest, Borrow) {
  Init();

  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_borrow(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);
}

TEST_F(StreamSocketTest, CreateWithType) {
  ASSERT_OK(zxio_create_with_type(storage(), ZXIO_OBJECT_TYPE_STREAM_SOCKET, TakeSocket().release(),
                                  &info(), /*is_connected=*/false,
                                  TakeClientEnd().TakeChannel().release()));
  ASSERT_OK(zxio_close(&storage()->io));
}

namespace {

class DatagramSocketTest : public zxtest::Test {
 public:
  void SetUp() final {
    ASSERT_OK(zx::socket::create(ZX_SOCKET_DATAGRAM, &socket_, &peer_));
    ASSERT_OK(socket_.get_info(ZX_INFO_SOCKET, &info_, sizeof(info_), nullptr, nullptr));

    zx::status server_end = fidl::CreateEndpoints(&client_end_);
    ASSERT_OK(server_end.status_value());

    fidl::BindServer(control_loop_.dispatcher(), std::move(*server_end), &server_);
    control_loop_.StartThread("control");
  }

  void Init() {
    ASSERT_OK(zxio_datagram_socket_init(&storage_, TakeSocket(), info(), prelude_size(),
                                        TakeClientEnd()));
    zxio_ = &storage_.io;
  }

  void TearDown() final {
    if (zxio_) {
      ASSERT_OK(zxio_close(zxio_));
    }
    control_loop_.Shutdown();
  }

  const zx_info_socket_t& info() const { return info_; }
  const zxio_datagram_prelude_size_t& prelude_size() const { return prelude_size_; }
  zx::socket TakeSocket() { return std::move(socket_); }
  fidl::ClientEnd<fuchsia_posix_socket::DatagramSocket> TakeClientEnd() {
    return std::move(client_end_);
  }
  zxio_storage_t* storage() { return &storage_; }
  zxio_t* zxio() { return zxio_; }

 private:
  zxio_storage_t storage_;
  zxio_t* zxio_{nullptr};
  zx_info_socket_t info_;
  const zxio_datagram_prelude_size_t prelude_size_{};
  zx::socket socket_, peer_;
  fidl::ClientEnd<fuchsia_posix_socket::DatagramSocket> client_end_;
  zxio_tests::DatagramSocketServer server_;
  async::Loop control_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

}  // namespace

TEST_F(DatagramSocketTest, Basic) { Init(); }

TEST_F(DatagramSocketTest, Release) {
  Init();

  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_release(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);

  EXPECT_OK(zx_handle_close(handle));
}

TEST_F(DatagramSocketTest, Borrow) {
  Init();

  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_borrow(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);
}

TEST_F(DatagramSocketTest, CreateWithType) {
  ASSERT_OK(zxio_create_with_type(storage(), ZXIO_OBJECT_TYPE_DATAGRAM_SOCKET,
                                  TakeSocket().release(), &info(), &prelude_size(),
                                  TakeClientEnd().TakeChannel().release()));
  ASSERT_OK(zxio_close(&storage()->io));
}

namespace {

class RawSocketTest : public zxtest::Test {
 public:
  void SetUp() final {
    ASSERT_OK(zx::eventpair::create(0u, &event_client_, &event_server_));

    zx::status server_end = fidl::CreateEndpoints(&client_end_);
    ASSERT_OK(server_end.status_value());

    fidl::BindServer(control_loop_.dispatcher(), std::move(*server_end), &server_);
    control_loop_.StartThread("control");
  }

  void Init() {
    ASSERT_OK(zxio_raw_socket_init(&storage_, TakeEventClient(), TakeClientEnd()));
    zxio_ = &storage_.io;
  }

  void TearDown() final {
    if (zxio_) {
      ASSERT_OK(zxio_close(zxio_));
    }
    control_loop_.Shutdown();
  }

  zx::eventpair TakeEventClient() { return std::move(event_client_); }
  fidl::ClientEnd<fuchsia_posix_socket_raw::Socket> TakeClientEnd() {
    return std::move(client_end_);
  }
  zxio_storage_t* storage() { return &storage_; }
  zxio_t* zxio() { return zxio_; }

 private:
  zxio_storage_t storage_;
  zxio_t* zxio_{nullptr};
  zx::eventpair event_client_, event_server_;
  fidl::ClientEnd<fuchsia_posix_socket_raw::Socket> client_end_;
  zxio_tests::RawSocketServer server_;
  async::Loop control_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

}  // namespace

TEST_F(RawSocketTest, Basic) { Init(); }

TEST_F(RawSocketTest, Release) {
  Init();
  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_release(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);

  EXPECT_OK(zx_handle_close(handle));
}

TEST_F(RawSocketTest, Borrow) {
  Init();
  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_borrow(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);
}

TEST_F(RawSocketTest, CreateWithType) {
  ASSERT_OK(zxio_create_with_type(storage(), ZXIO_OBJECT_TYPE_RAW_SOCKET,
                                  TakeEventClient().release(),
                                  TakeClientEnd().TakeChannel().release()));
  ASSERT_OK(zxio_close(&storage()->io));
}

namespace {

class PacketSocketTest : public zxtest::Test {
 public:
  void SetUp() final {
    ASSERT_OK(zx::eventpair::create(0u, &event_client_, &event_server_));

    zx::status server_end = fidl::CreateEndpoints(&client_end_);
    ASSERT_OK(server_end.status_value());

    fidl::BindServer(control_loop_.dispatcher(), std::move(*server_end), &server_);
    control_loop_.StartThread("control");
  }

  void Init() {
    ASSERT_OK(zxio_packet_socket_init(&storage_, TakeEventClient(), TakeClientEnd()));
    zxio_ = &storage_.io;
  }

  void TearDown() final {
    if (zxio_) {
      ASSERT_OK(zxio_close(zxio_));
    }
    control_loop_.Shutdown();
  }

  zx::eventpair TakeEventClient() { return std::move(event_client_); }
  fidl::ClientEnd<fuchsia_posix_socket_packet::Socket> TakeClientEnd() {
    return std::move(client_end_);
  }
  zxio_storage_t* storage() { return &storage_; }
  zxio_t* zxio() { return zxio_; }

 private:
  zxio_storage_t storage_;
  zxio_t* zxio_{nullptr};
  zx::eventpair event_client_, event_server_;
  fidl::ClientEnd<fuchsia_posix_socket_packet::Socket> client_end_;
  zxio_tests::PacketSocketServer server_;
  async::Loop control_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

}  // namespace

TEST_F(PacketSocketTest, Basic) { Init(); }

TEST_F(PacketSocketTest, Release) {
  Init();
  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_release(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);

  EXPECT_OK(zx_handle_close(handle));
}

TEST_F(PacketSocketTest, Borrow) {
  Init();
  zx_handle_t handle = ZX_HANDLE_INVALID;
  EXPECT_OK(zxio_borrow(zxio(), &handle));
  EXPECT_NE(handle, ZX_HANDLE_INVALID);
}

TEST_F(PacketSocketTest, CreateWithType) {
  ASSERT_OK(zxio_create_with_type(storage(), ZXIO_OBJECT_TYPE_PACKET_SOCKET,
                                  TakeEventClient().release(),
                                  TakeClientEnd().TakeChannel().release()));
  ASSERT_OK(zxio_close(&storage()->io));
}
