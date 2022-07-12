// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/tests/fidl/compatibility/helpers.h>

namespace fidl_test_compatibility_helpers {

// Helper text for how to invoke the proper compatibility test combination.
constexpr char kUsage[] = ("Usage:\n  fidl_compatibility_test foo_server bar_server\n");

AllowServerPair Exclude(std::initializer_list<const char*> substrings) {
  return [substrings](const std::string& proxy_url, const std::string& server_url) {
    for (auto substring : substrings) {
      if (proxy_url.find(substring) != std::string::npos) {
        return false;
      }
      if (server_url.find(substring) != std::string::npos) {
        return false;
      }
    }
    return true;
  };
}

std::string ExtractShortName(const std::string& pkg_url) {
  std::string short_name;
  re2::RE2::PartialMatch(pkg_url, "(meta/fidl_compatibility_test_server_)(.*)(\\.cmx)", nullptr,
                         nullptr, &short_name);
  return short_name;
}

void ForAllServers(Servers servers, TestBody body) {
  ForSomeServers(
      servers, [](const std::string& _p, const std::string& _s) { return true; }, body);
}

void ForSomeServers(Servers servers, AllowServerPair allow, TestBody body) {
  for (auto const& proxy_url : servers) {
    for (auto const& server_url : servers) {
      if (!allow(proxy_url, server_url)) {
        continue;
      }
      std::cerr << proxy_url << " <-> " << server_url << std::endl;
      async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
      fidl::test::compatibility::EchoClientApp proxy;
      bool test_completed = false;
      proxy.echo().set_error_handler([&proxy_url, &loop, &test_completed](zx_status_t status) {
        if (!test_completed) {
          loop.Quit();
          FAIL() << "Connection to " << proxy_url << " failed unexpectedly: " << status;
        }
      });
      proxy.Start(proxy_url);

      body(loop, proxy.echo(), server_url, proxy_url);
      test_completed = true;
    }
  }
}

bool GetServersUnderTest(int argc, char** argv, Servers* out_servers) {
  for (int i = 1; i < argc; i++) {
    std::string server(argv[i]);
    std::string package_url;
    if (server.rfind("fuchsia-pkg://", 0) == 0) {
      package_url = server;
    } else {
      package_url = "fuchsia-pkg://fuchsia.com/fidl-compatibility-test#meta/" +
                    std::string(argv[i]) + "-server.cmx";
    }
    out_servers->push_back(package_url);
  }

  if (!out_servers->empty()) {
    return true;
  }
  FX_CHECK(!out_servers->empty()) << kUsage;
  return false;
}

zx::handle Handle() {
  zx_handle_t raw_event;
  const zx_status_t status = zx_event_create(0u, &raw_event);
  ZX_ASSERT_MSG(status == ZX_OK, "status = %d", status);
  return zx::handle(raw_event);
}

::testing::AssertionResult HandlesEq(const zx::object_base& a, const zx::object_base& b) {
  if (a.is_valid() != b.is_valid()) {
    return ::testing::AssertionFailure()
           << "Handles are not equally valid :" << a.is_valid() << " vs " << b.is_valid();
  }
  if (!a.is_valid()) {
    return ::testing::AssertionSuccess() << "Both handles invalid";
  }
  zx_info_handle_basic_t a_info, b_info;
  zx_status_t status =
      zx_object_get_info(a.get(), ZX_INFO_HANDLE_BASIC, &a_info, sizeof(a_info), nullptr, nullptr);
  if (ZX_OK != status) {
    return ::testing::AssertionFailure() << "zx_object_get_info(a) returned " << status;
  }
  status =
      zx_object_get_info(b.get(), ZX_INFO_HANDLE_BASIC, &b_info, sizeof(b_info), nullptr, nullptr);
  if (ZX_OK != status) {
    return ::testing::AssertionFailure() << "zx_object_get_info(b) returned " << status;
  }
  if (a_info.koid != b_info.koid) {
    return ::testing::AssertionFailure() << std::endl
                                         << "a_info.koid is: " << a_info.koid << std::endl
                                         << "b_info.koid is: " << b_info.koid;
  }
  return ::testing::AssertionSuccess();
}

void PrintSummary(const Summary& summary) {
  std::cout << std::endl;
  std::cout << "========================= Interop Summary ======================" << std::endl;

  for (std::pair<std::string, bool> element : summary) {
    if (element.second) {
      std::cout << "[PASS]";
    } else {
      std::cout << "[FAIL]";
    }
    std::cout << " " << element.first << std::endl;
  }

  std::cout << std::endl;
  std::cout << std::endl;
}

std::string RandomUTF8(size_t count, std::default_random_engine& rand_engine) {
  std::uniform_int_distribution<uint32_t> uint32_distribution;
  std::string random_string;
  random_string.reserve(count);
  do {
    // Generate a random 32 bit unsigned int to use a the code point.
    uint32_t code_point = uint32_distribution(rand_engine);
    // Mask the random number so that it can be encoded into the number of bytes
    // remaining.
    size_t remaining = count - random_string.size();
    if (remaining == 1) {
      code_point &= 0x7F;
    } else if (remaining == 2) {
      code_point &= 0x7FF;
    } else if (remaining == 3) {
      code_point &= 0xFFFF;
    } else {
      // Mask to fall within the general range of code points.
      code_point &= 0x1FFFFF;
    }
    // Check that it's really a valid code point, otherwise try again.
    if (!fxl::IsValidCodepoint(code_point)) {
      continue;
    }
    // Add the character to the random string.
    fxl::WriteUnicodeCharacter(code_point, &random_string);
    FX_CHECK(random_string.size() <= count);
  } while (random_string.size() < count);
  return random_string;
}

}  // namespace fidl_test_compatibility_helpers
