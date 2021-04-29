// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/dwarf_location.h"

#include <gtest/gtest.h>

namespace zxdb {

TEST(DwarfLocation, UnitRelative) {
  // This has the first range with an empty expression, the second with an empty range. These do
  // not get added to the result.
  std::vector<uint8_t> data{
      0x4a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Begin of range 0.
      0xf6, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // End of range 0.
      0x00, 0x00,                                      // Expression 0 byte length.
      0xf8, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Begin of range 1.
      0xf8, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // End of range 1.
      0x03, 0x00,                                      // Expression 1 byte length.
      0x7f, 0xd8, 0x75,                                // Expression 1.
      0xf8, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Begin of range 2.
      0x51, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // End of range 1.
      0x03, 0x00,                                      // Expression 2 byte length.
      0x7f, 0xd8, 0x75,                                // Expression 2.
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // End of expression marker (0, 0).
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  constexpr TargetPointer kUnitBaseAddress = 0x1000000;
  VariableLocation result = DecodeLocationList(kUnitBaseAddress, data, LazySymbol());
  EXPECT_FALSE(result.is_null());
  ASSERT_EQ(1u, result.locations().size());  // Only the last range was nonempty.

  EXPECT_EQ(kUnitBaseAddress + 0xdf8, result.locations()[0].begin);
  EXPECT_EQ(kUnitBaseAddress + 0x1051, result.locations()[0].end);
  std::vector<uint8_t> expected_expr{0x7f, 0xd8, 0x75};
  EXPECT_EQ(expected_expr, result.locations()[0].expression.data());
}

TEST(DwarfLocation, NewBaseAddress) {
  // This is a real location list generated by Clang.
  std::vector<uint8_t> data{
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // Base address selector.
      0x20, 0x20, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,  // New base addr.
      0x4a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Begin of range 0.
      0xf6, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // End of range 0.
      0x03, 0x00,                                      // Expression 0 byte length.
      0x7f, 0xd8, 0x75,                                // Expression 0.
      0xf8, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Begin of range 1.
      0x51, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // End of range 1.
      0x03, 0x00,                                      // Expression 1 byte length.
      0x7f, 0xd8, 0x75,                                // Expression 1.
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // End of expression marker (0, 0).
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  VariableLocation result = DecodeLocationList(0, data, LazySymbol());
  EXPECT_FALSE(result.is_null());
  ASSERT_EQ(2u, result.locations().size());

  EXPECT_EQ(0x2206au, result.locations()[0].begin);
  EXPECT_EQ(0x22e16u, result.locations()[0].end);
  std::vector<uint8_t> expected_expr{0x7f, 0xd8, 0x75};
  EXPECT_EQ(expected_expr, result.locations()[0].expression.data());

  EXPECT_EQ(0x22e18u, result.locations()[1].begin);
  EXPECT_EQ(0x23071u, result.locations()[1].end);
  EXPECT_EQ(expected_expr, result.locations()[1].expression.data());

  // Now test a truncated list.
  result = DecodeLocationList(0, containers::array_view<uint8_t>(&data[0], data.size() - 4),
                              LazySymbol());
  EXPECT_TRUE(result.is_null());
}

TEST(DwarfLocation, BadExpr) {
  // This expression has a byte length that's too long for the data.
  std::vector<uint8_t> data{0x4a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Begin of range.
                            0xf6, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // End of range.
                            0x00, 0x80,         // Expression byte length.
                            0x7f, 0xd8, 0x75};  // Expression (shorter than byte length).

  VariableLocation result = DecodeLocationList(0, data, LazySymbol());
  EXPECT_TRUE(result.is_null());
}

}  // namespace zxdb
