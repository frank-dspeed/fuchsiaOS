// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oob_doubler.h"

#include <ddktl/protocol/nand.h>
#include <unittest/unittest.h>

namespace {

constexpr uint32_t kPageSize = 100;
constexpr uint32_t kOobSize = 10;
constexpr uint32_t kBlockSize = 50;
constexpr size_t kOpSize = 42;
constexpr size_t kUnchanged = 20;

class NandTester : public ddk::NandProtocol<NandTester> {
  public:
    NandTester() : proto_({&nand_protocol_ops_, this}) {
        info_.page_size = kPageSize;
        info_.oob_size = kOobSize;
        info_.pages_per_block = kBlockSize;
        info_.num_blocks = kUnchanged;
        info_.ecc_bits = kUnchanged;
    }

    nand_protocol_t* proto() { return &proto_; }
    nand_operation_t* operation() { return &operation_; }

    void NandQuery(zircon_nand_Info* out_info, size_t* out_nand_op_size) {
        *out_info = info_;
        *out_nand_op_size = kOpSize;
    }

    void NandQueue(nand_operation_t* operation, nand_queue_callback callback, void* cookie) {
        operation_ = *operation;
    }

    zx_status_t NandGetFactoryBadBlockList(uint32_t* out_bad_blocks_list, size_t bad_blocks_count,
                                           size_t* out_bad_blocks_actual) {
      return ZX_OK;
    }

  private:
    nand_protocol_t proto_;
    zircon_nand_Info info_ = {};
    nand_operation_t operation_ = {};
};

bool TrivialLifetimeTest() {
    BEGIN_TEST;
    NandTester tester;
    ftl::OobDoubler doubler(tester.proto(), false);
    END_TEST;
}

bool QueryDisabledTest() {
    BEGIN_TEST;
    NandTester tester;
    ftl::OobDoubler doubler(tester.proto(), false);

    zircon_nand_Info info;
    size_t op_size;
    doubler.Query(&info, &op_size);
    EXPECT_EQ(kPageSize, info.page_size);
    EXPECT_EQ(kOobSize, info.oob_size);
    EXPECT_EQ(kBlockSize, info.pages_per_block);
    EXPECT_EQ(kUnchanged, info.num_blocks);
    EXPECT_EQ(kUnchanged, info.ecc_bits);
    EXPECT_EQ(kOpSize, op_size);
    END_TEST;
}

bool QueryEnabledTest() {
    BEGIN_TEST;
    NandTester tester;
    ftl::OobDoubler doubler(tester.proto(), true);

    zircon_nand_Info info;
    size_t op_size;
    doubler.Query(&info, &op_size);
    EXPECT_EQ(kPageSize * 2, info.page_size);
    EXPECT_EQ(kOobSize * 2, info.oob_size);
    EXPECT_EQ(kBlockSize / 2, info.pages_per_block);
    EXPECT_EQ(kUnchanged, info.num_blocks);
    EXPECT_EQ(kUnchanged, info.ecc_bits);
    EXPECT_EQ(kOpSize, op_size);
    END_TEST;
}

bool QueueDisabledTest() {
    BEGIN_TEST;
    NandTester tester;
    ftl::OobDoubler doubler(tester.proto(), false);

    nand_operation_t op = {};
    op.command = NAND_OP_READ;
    op.rw.length = 5;
    op.rw.offset_nand = 6;
    op.rw.offset_data_vmo = 7;
    op.rw.offset_oob_vmo = 8;
    doubler.Queue(&op, nullptr, nullptr);

    nand_operation_t* result = tester.operation();
    EXPECT_EQ(NAND_OP_READ, result->command);
    EXPECT_EQ(5, result->rw.length);
    EXPECT_EQ(6, result->rw.offset_nand);
    EXPECT_EQ(7, result->rw.offset_data_vmo);
    EXPECT_EQ(8, result->rw.offset_oob_vmo);
    END_TEST;
}

bool QueueEnabledTest() {
    BEGIN_TEST;
    NandTester tester;
    ftl::OobDoubler doubler(tester.proto(), true);

    nand_operation_t op = {};
    op.command = NAND_OP_READ;
    op.rw.length = 5;
    op.rw.offset_nand = 6;
    op.rw.offset_data_vmo = 7;
    op.rw.offset_oob_vmo = 8;
    doubler.Queue(&op, nullptr, nullptr);

    nand_operation_t* result = tester.operation();
    EXPECT_EQ(NAND_OP_READ, result->command);
    EXPECT_EQ(10, result->rw.length);
    EXPECT_EQ(12, result->rw.offset_nand);
    EXPECT_EQ(14, result->rw.offset_data_vmo);
    EXPECT_EQ(16, result->rw.offset_oob_vmo);
    END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(OobDoublerTests)
RUN_TEST_SMALL(TrivialLifetimeTest)
RUN_TEST_SMALL(QueryDisabledTest)
RUN_TEST_SMALL(QueryEnabledTest)
RUN_TEST_SMALL(QueueDisabledTest)
RUN_TEST_SMALL(QueueEnabledTest)
END_TEST_CASE(OobDoublerTests)
