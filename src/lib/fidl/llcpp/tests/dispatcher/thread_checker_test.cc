// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/internal/debug_thread_checker.h>

#include <thread>

#include <zxtest/zxtest.h>

#ifndef NDEBUG

// |ThreadChecker| should check that it is always used from the same thread in
// debug builds.
TEST(ThreadChecker, CheckInDebug) {
  fidl::internal::DebugOnlyThreadChecker checker{
      std::in_place_type_t<fidl::internal::ZirconThreadChecker>{},
      fidl::internal::ThreadingPolicy::kCreateAndTeardownFromDispatcherThread};
  std::thread thread(
      [&] { ASSERT_DEATH([&] { fidl::internal::ScopedThreadGuard guard(checker); }); });
  thread.join();
}

// It is possible to configure whether to skip the check.
TEST(ThreadChecker, SkipCheckUsingPolicy) {
  fidl::internal::DebugOnlyThreadChecker checker{
      std::in_place_type_t<fidl::internal::ZirconThreadChecker>{},
      fidl::internal::ThreadingPolicy::kCreateAndTeardownFromAnyThread};
  std::thread thread(
      [&] { ASSERT_NO_DEATH([&] { fidl::internal::ScopedThreadGuard guard(checker); }); });
  thread.join();
}

#else

// |ThreadChecker| should not perform any assertions in release builds.
TEST(ThreadChecker, NoCheckInRelease) {
  fidl::internal::DebugOnlyThreadChecker checker{
      std::in_place_type_t<fidl::internal::ZirconThreadChecker>{},
      fidl::internal::ThreadingPolicy::kCreateAndTeardownFromDispatcherThread};
  std::thread thread(
      [&] { ASSERT_NO_DEATH([&] { fidl::internal::ScopedThreadGuard guard(checker); }); });
  thread.join();
}

#endif  // NDEBUG
