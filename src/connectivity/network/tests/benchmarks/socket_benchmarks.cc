// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <lib/syslog/cpp/macros.h>
#include <netinet/icmp6.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include <array>
#include <iostream>
#include <string_view>

#include <fbl/unique_fd.h>
#include <perftest/perftest.h>

#include "src/lib/fxl/strings/string_printf.h"

namespace {

#define CHECK_TRUE_ERRNO(true_condition) FX_CHECK(true_condition) << strerror(errno)
#define CHECK_ZERO_ERRNO(value)                                                          \
  do {                                                                                   \
    auto c = (value);                                                                    \
    FX_CHECK(c == 0) << "expected zero, got " << c << " with errno " << strerror(errno); \
  } while (0)

#define CHECK_POSITIVE(value)                            \
  do {                                                   \
    if (auto c = (value); c <= 0) {                      \
      FX_CHECK(c != 0) << "expected nonzero, got " << c; \
      FX_LOGS(FATAL) << strerror(errno);                 \
    }                                                    \
  } while (0)

template <typename T>
class AddrStorage {
 public:
  static_assert(std::is_same_v<T, sockaddr_in> || std::is_same_v<T, sockaddr_in6>);
  sockaddr* as_sockaddr() { return reinterpret_cast<sockaddr*>(&addr); }
  const sockaddr* as_sockaddr() const { return reinterpret_cast<const sockaddr*>(&addr); }
  socklen_t socklen() const { return sizeof(addr); }
  T addr;
};

class Ipv6 {
 public:
  using SockAddr = AddrStorage<sockaddr_in6>;
  static constexpr int kFamily = AF_INET6;
  static constexpr int kIpProtoIcmp = IPPROTO_ICMPV6;
  static constexpr uint8_t kIcmpEchoRequestType = ICMP6_ECHO_REQUEST;
  static constexpr uint8_t kIcmpEchoReplyType = ICMP6_ECHO_REPLY;

  static SockAddr loopback() {
    return {
        .addr =
            {
                .sin6_family = kFamily,
                .sin6_addr = IN6ADDR_LOOPBACK_INIT,
            },
    };
  }
};

class Ipv4 {
 public:
  using SockAddr = AddrStorage<sockaddr_in>;
  static constexpr int kFamily = AF_INET;
  static constexpr int kIpProtoIcmp = IPPROTO_ICMP;
  static constexpr uint8_t kIcmpEchoRequestType = ICMP_ECHO;
  static constexpr uint8_t kIcmpEchoReplyType = ICMP_ECHOREPLY;

  static SockAddr loopback() {
    return {
        .addr =
            {
                .sin_family = kFamily,
                .sin_addr =
                    {
                        .s_addr = htonl(INADDR_LOOPBACK),
                    },
            },
    };
  }
};

// Helper no-op function to assert functions abstracted over IP version are properly parameterized.
template <typename Ip>
void TemplateIsIpVersion() {
  static_assert(std::is_same_v<Ip, Ipv4> || std::is_same_v<Ip, Ipv6>);
}

// Computes the unidirectional throughput on a TCP loopback socket.
//
// Measures the time to write `transfer` bytes on one end of the socket and read them on the other
// end on the same thread and calculates the throughput.
template <typename Ip>
bool TcpWriteRead(perftest::RepeatState* state, size_t transfer) {
  TemplateIsIpVersion<Ip>();
  using Addr = typename Ip::SockAddr;
  fbl::unique_fd listen_sock;
  CHECK_TRUE_ERRNO(listen_sock = fbl::unique_fd(socket(Ip::kFamily, SOCK_STREAM, 0)));
  Addr sockaddr = Ip::loopback();
  CHECK_ZERO_ERRNO(bind(listen_sock.get(), sockaddr.as_sockaddr(), sockaddr.socklen()));
  CHECK_ZERO_ERRNO(listen(listen_sock.get(), 0));

  socklen_t socklen = sockaddr.socklen();
  CHECK_ZERO_ERRNO(getsockname(listen_sock.get(), sockaddr.as_sockaddr(), &socklen));

  fbl::unique_fd client_sock;
  CHECK_TRUE_ERRNO(client_sock = fbl::unique_fd(socket(Ip::kFamily, SOCK_STREAM, 0)));

  // Set send buffer to transfer size to ensure we can write `transfer` bytes before reading it on
  // the other end.
  FX_CHECK(transfer < std::numeric_limits<int32_t>::max());
  int32_t sndbuf = static_cast<int32_t>(transfer);
  CHECK_ZERO_ERRNO(setsockopt(client_sock.get(), SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)));
  // Disable the Nagle algorithm, it introduces artificial latency that defeats this test.
  const int32_t no_delay = 1;
  CHECK_ZERO_ERRNO(
      setsockopt(client_sock.get(), SOL_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay)));

  CHECK_ZERO_ERRNO(connect(client_sock.get(), sockaddr.as_sockaddr(), sockaddr.socklen()));

  fbl::unique_fd server_sock;
  CHECK_TRUE_ERRNO(server_sock = fbl::unique_fd(accept(listen_sock.get(), nullptr, nullptr)));

  std::vector<uint8_t> send_bytes, recv_bytes;
  // Avoid large memory regions with zeroes that can cause the system to try and reclaim pages from
  // us. For more information see Zircon page scanner and eviction strategies.
  send_bytes.resize(transfer, 0xAA);
  recv_bytes.resize(transfer, 0xBB);

  state->SetBytesProcessedPerRun(transfer);
  while (state->KeepRunning()) {
    for (size_t sent = 0; sent < transfer;) {
      ssize_t wr = write(client_sock.get(), send_bytes.data() + sent, transfer - sent);
      CHECK_POSITIVE(wr);
      sent += wr;
    }
    for (size_t recv = 0; recv < transfer;) {
      ssize_t rd = read(server_sock.get(), recv_bytes.data() + recv, transfer - recv);
      CHECK_POSITIVE(rd);
      recv += rd;
    }
  }

  return true;
}

// Computes unidirectional throughput on a UDP loopback socket.
//
// Measures the time to write `message_count` messages of size `message_size`
// bytes on one end of the socket and read them out on the other end on the
// same thread and calculates the throughput.
template <typename Ip>
bool UdpWriteRead(perftest::RepeatState* state, size_t message_size, size_t message_count) {
  TemplateIsIpVersion<Ip>();
  using Addr = typename Ip::SockAddr;

  fbl::unique_fd server_sock;
  CHECK_TRUE_ERRNO(server_sock = fbl::unique_fd(socket(Ip::kFamily, SOCK_DGRAM, 0)));
  Addr sockaddr = Ip::loopback();
  CHECK_ZERO_ERRNO(bind(server_sock.get(), sockaddr.as_sockaddr(), sockaddr.socklen()));

  uint32_t rcvbuf_opt;
  socklen_t rcvbuf_optlen = sizeof(rcvbuf_opt);
  CHECK_ZERO_ERRNO(
      getsockopt(server_sock.get(), SOL_SOCKET, SO_RCVBUF, &rcvbuf_opt, &rcvbuf_optlen));

  // On Linux, payloads are stored with a fixed per-packet overhead. Linux
  // accounts for this overhead by setting the actual buffer size to double
  // the size set with SO_RCVBUF. This hack fails when SO_RCVBUF is small and
  // many packets are sent; avoid that case by setting RCVBUF only when the
  // bytes-to-be-sent exceed the default value (which is large).
  if (rcvbuf_opt < message_size * message_count) {
    int rcv_bufsize = static_cast<int>(message_size * message_count);
    CHECK_ZERO_ERRNO(
        setsockopt(server_sock.get(), SOL_SOCKET, SO_RCVBUF, &rcv_bufsize, sizeof(rcv_bufsize)));
    CHECK_ZERO_ERRNO(
        getsockopt(server_sock.get(), SOL_SOCKET, SO_RCVBUF, &rcvbuf_opt, &rcvbuf_optlen));
  }

  FX_CHECK(rcvbuf_opt >= message_size * message_count)
      << "rcvbuf size (" << rcvbuf_opt << ") < transfer size (" << message_size * message_count
      << ")";

  socklen_t socklen = sockaddr.socklen();
  CHECK_ZERO_ERRNO(getsockname(server_sock.get(), sockaddr.as_sockaddr(), &socklen));

  fbl::unique_fd client_sock;
  CHECK_TRUE_ERRNO(client_sock = fbl::unique_fd(socket(Ip::kFamily, SOCK_DGRAM, 0)));
  CHECK_ZERO_ERRNO(connect(client_sock.get(), sockaddr.as_sockaddr(), sockaddr.socklen()));

  std::vector<uint8_t> send_bytes, recv_bytes;
  // Avoid large memory regions with zeroes that can cause the system to try and reclaim pages from
  // us. For more information see Zircon page scanner and eviction strategies.
  send_bytes.resize(message_size, 0xAA);
  recv_bytes.resize(message_size, 0xBB);

  state->SetBytesProcessedPerRun(message_size);
  while (state->KeepRunning()) {
    for (size_t i = 0; i < message_count; i++) {
      ssize_t wr = write(client_sock.get(), send_bytes.data(), message_size);
      CHECK_TRUE_ERRNO(wr >= 0);
      FX_CHECK(static_cast<size_t>(wr) == message_size)
          << "wrote " << wr << " expected " << message_size;
    }
    for (size_t i = 0; i < message_count; i++) {
      ssize_t rd = read(server_sock.get(), recv_bytes.data(), message_size);
      CHECK_TRUE_ERRNO(rd >= 0);
      FX_CHECK(static_cast<size_t>(rd) == message_size)
          << "read " << rd << " expected " << message_size;
    }
  }

  return true;
}

// Tests the ping latency over a loopback socket.
//
// Measures the time to send an echo request over a loopback ICMP socket and observe its response.
template <typename Ip>
bool PingLatency(perftest::RepeatState* state) {
  TemplateIsIpVersion<Ip>();
  using Addr = typename Ip::SockAddr;

  fbl::unique_fd sock;
  CHECK_TRUE_ERRNO(sock = fbl::unique_fd(socket(Ip::kFamily, SOCK_DGRAM, Ip::kIpProtoIcmp)));
  const Addr sockaddr = Ip::loopback();
  CHECK_ZERO_ERRNO(connect(sock.get(), sockaddr.as_sockaddr(), sockaddr.socklen()));

  struct {
    icmphdr icmp;
    char payload[4];
  } send_buffer, recv_buffer;
  uint16_t sequence = 0;
  icmphdr& send_header = send_buffer.icmp;

  while (state->KeepRunning()) {
    send_header = {
        .type = Ip::kIcmpEchoRequestType,
        .un = {.echo = {.sequence = ++sequence}},
    };
    ssize_t wr = write(sock.get(), &send_buffer, sizeof(send_buffer));
    CHECK_TRUE_ERRNO(wr >= 0);
    FX_CHECK(static_cast<size_t>(wr) == sizeof(send_buffer))
        << "wrote " << wr << " expected " << sizeof(send_buffer);

    ssize_t rd = read(sock.get(), &recv_buffer, sizeof(recv_buffer));
    CHECK_TRUE_ERRNO(rd >= 0);
    FX_CHECK(static_cast<size_t>(rd) == sizeof(recv_buffer))
        << "read " << rd << " expected " << sizeof(recv_buffer);
    const icmphdr& header = recv_buffer.icmp;
    FX_CHECK(header.type == Ip::kIcmpEchoReplyType)
        << "received header type " << header.type << ", expected echo response "
        << Ip::kIcmpEchoReplyType;
    FX_CHECK(header.un.echo.sequence == sequence)
        << "received sequence " << header.un.echo.sequence << ", expected sequence " << sequence;
  }

  return true;
}

constexpr char kFakeNetstackEnvVar[] = "FAKE_NETSTACK";

void RegisterTests() {
  constexpr std::string_view kSingleReadTestNameFmt = "WriteRead/%s/%s/%ld%s";
  enum class Network { kIpv4, kIpv6 };

  auto network_to_string = [](Network network) {
    switch (network) {
      case Network::kIpv4:
        return "IPv4";
      case Network::kIpv6:
        return "IPv6";
    }
  };

  auto bytes_with_unit = [](size_t bytes) -> std::pair<size_t, std::string_view> {
    if (bytes >= 1024) {
      bytes /= 1024;
      // Keep "kB" instead of "KiB" to avoid losing benchmarking history.
      return {bytes, "kB"};
    }
    return {bytes, "B"};
  };

  auto get_tcp_test_name = [&bytes_with_unit, &network_to_string, &kSingleReadTestNameFmt](
                               Network network, size_t raw_bytes) -> std::string {
    std::string_view network_name = network_to_string(network);
    auto [bytes, bytes_unit] = bytes_with_unit(raw_bytes);

    return fxl::StringPrintf(kSingleReadTestNameFmt.data(), "TCP", network_name.data(), bytes,
                             bytes_unit.data());
  };

  auto get_udp_test_name = [&bytes_with_unit, &network_to_string, &kSingleReadTestNameFmt](
                               Network network, size_t raw_bytes,
                               size_t message_count) -> std::string {
    std::string_view network_name = network_to_string(network);
    auto [bytes, bytes_unit] = bytes_with_unit(raw_bytes);
    constexpr std::string_view kUDP = "UDP";
    if (message_count > 1) {
      return fxl::StringPrintf("MultiWriteRead/%s/%s/%ld%s/%ldMessages", kUDP.data(),
                               network_name.data(), bytes, bytes_unit.data(), message_count);
    } else {
      return fxl::StringPrintf(kSingleReadTestNameFmt.data(), kUDP.data(), network_name.data(),
                               bytes, bytes_unit.data());
    }
  };

  constexpr size_t kTransferSizesForTcp[] = {
      1 << 10, 10 << 10, 100 << 10, 500 << 10, 1000 << 10,
  };
  for (size_t transfer : kTransferSizesForTcp) {
    perftest::RegisterTest(get_tcp_test_name(Network::kIpv4, transfer).c_str(), TcpWriteRead<Ipv4>,
                           transfer);
    perftest::RegisterTest(get_tcp_test_name(Network::kIpv6, transfer).c_str(), TcpWriteRead<Ipv6>,
                           transfer);
  }

  // NB: Knowledge encoded at a distance: these datagrams avoid IP fragmentation
  // only because loopback has a very large MTU.
  constexpr size_t kMessageSizesForUdp[] = {1, 100, 1 << 10, 10 << 10, 60 << 10};
  constexpr size_t kMessageCountsForUdp[] = {1, 10, 50};
  for (size_t message_size : kMessageSizesForUdp) {
    for (size_t message_count : kMessageCountsForUdp) {
      perftest::RegisterTest(get_udp_test_name(Network::kIpv4, message_size, message_count).c_str(),
                             UdpWriteRead<Ipv4>, message_size, message_count);
      perftest::RegisterTest(get_udp_test_name(Network::kIpv6, message_size, message_count).c_str(),
                             UdpWriteRead<Ipv6>, message_size, message_count);
    }
  }

  [&network_to_string]() {
#if !defined(__Fuchsia__)
    // When running on not-Fuchsia, we may not be permitted to create ICMP sockets.
    if (int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP); fd < 0) {
      if (errno == EACCES) {
        std::cout << "ICMP sockets are not permitted; skipping ping benchmarks" << std::endl;
        return;
      }
    } else {
      CHECK_ZERO_ERRNO(close(fd));
    }
#endif

    constexpr char kPingTestNameFmt[] = "PingLatency/%s";
    perftest::RegisterTest(
        fxl::StringPrintf(kPingTestNameFmt, network_to_string(Network::kIpv4)).c_str(),
        PingLatency<Ipv4>);
    perftest::RegisterTest(
        fxl::StringPrintf(kPingTestNameFmt, network_to_string(Network::kIpv6)).c_str(),
        PingLatency<Ipv6>);
  }();
}

PERFTEST_CTOR(RegisterTests)
}  // namespace

int main(int argc, char** argv) {
  std::string test_suite = "fuchsia.network.socket.loopback";
  if (std::getenv("FAST_UDP")) {
    test_suite += ".fastudp";
  } else if (std::getenv(kFakeNetstackEnvVar)) {
    test_suite += ".fake_netstack";
  }
  return perftest::PerfTestMain(argc, argv, test_suite.c_str());
}
