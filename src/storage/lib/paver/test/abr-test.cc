// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/abr/abr.h"

#include <endian.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/cksum.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <lib/service/llcpp/service.h>
#include <zircon/hw/gpt.h>

#include <iostream>

#include <chromeos-disk-setup/chromeos-disk-setup.h>
#include <mock-boot-arguments/server.h>
#include <zxtest/zxtest.h>

#include "gpt/cros.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"
#include "src/storage/lib/paver/abr-client-vboot.h"
#include "src/storage/lib/paver/abr-client.h"
#include "src/storage/lib/paver/astro.h"
#include "src/storage/lib/paver/chromebook-x64.h"
#include "src/storage/lib/paver/luis.h"
#include "src/storage/lib/paver/sherlock.h"
#include "src/storage/lib/paver/test/test-utils.h"
#include "src/storage/lib/paver/x64.h"

namespace {

using devmgr_integration_test::RecursiveWaitForFile;
using driver_integration_test::IsolatedDevmgr;

TEST(AstroAbrTests, CreateFails) {
  IsolatedDevmgr devmgr;
  IsolatedDevmgr::Args args;
  args.driver_search_paths.push_back("/boot/driver");
  args.disable_block_watcher = false;
  args.board_name = "sherlock";

  ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr));
  fbl::unique_fd fd;
  ASSERT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform", &fd));

  fidl::ClientEnd<fuchsia_io::Directory> svc_root;
  ASSERT_NOT_OK(
      paver::AstroAbrClientFactory().New(devmgr.devfs_root().duplicate(), svc_root, nullptr));
}

TEST(SherlockAbrTests, CreateFails) {
  IsolatedDevmgr devmgr;
  IsolatedDevmgr::Args args;
  args.driver_search_paths.push_back("/boot/driver");
  args.disable_block_watcher = false;
  args.board_name = "astro";

  ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr));
  fbl::unique_fd fd;
  ASSERT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform", &fd));

  auto svc_root = service::ConnectAt<fuchsia_io::Directory>(devmgr.fshost_outgoing_dir(), "svc");
  ASSERT_OK(svc_root.status_value());

  ASSERT_NOT_OK(paver::SherlockAbrClientFactory().Create(devmgr.devfs_root().duplicate(), *svc_root,
                                                         nullptr));
}

TEST(LuisAbrTests, CreateFails) {
  IsolatedDevmgr devmgr;
  IsolatedDevmgr::Args args;
  args.driver_search_paths.push_back("/boot/driver");
  args.disable_block_watcher = false;
  args.board_name = "astro";

  ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr));
  fbl::unique_fd fd;
  ASSERT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform", &fd));

  auto svc_root = service::ConnectAt<fuchsia_io::Directory>(devmgr.fshost_outgoing_dir(), "svc");
  ASSERT_OK(svc_root.status_value());

  ASSERT_NOT_OK(
      paver::LuisAbrClientFactory().Create(devmgr.devfs_root().duplicate(), *svc_root, nullptr));
}

TEST(X64AbrTests, CreateFails) {
  IsolatedDevmgr devmgr;
  IsolatedDevmgr::Args args;
  args.driver_search_paths.push_back("/boot/driver");
  args.disable_block_watcher = false;
  args.board_name = "x64";

  ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr));
  fbl::unique_fd fd;
  ASSERT_OK(RecursiveWaitForFile(devmgr.devfs_root(), "sys/platform", &fd));

  auto svc_root = service::ConnectAt<fuchsia_io::Directory>(devmgr.fshost_outgoing_dir(), "svc");
  ASSERT_OK(svc_root.status_value());

  ASSERT_NOT_OK(
      paver::X64AbrClientFactory().Create(devmgr.devfs_root().duplicate(), *svc_root, nullptr));
}

class ChromebookX64AbrTests : public zxtest::Test {
  static constexpr int kBlockSize = 512;
  static constexpr uint64_t kZxPartBlocks = SZ_ZX_PART / kBlockSize;
  static constexpr uint64_t kMinFvmSize = (8LU * (1 << 30)) / kBlockSize;
  static constexpr uint64_t kSysCfgSize = (1 << 20) / 512;
  // we need at least 3 * SZ_ZX_PART for zircon a/b/r, and kMinFvmSize for fvm.
  static constexpr uint64_t kDiskBlocks = 4 * kZxPartBlocks + kMinFvmSize;
  static constexpr uint8_t kEmptyType[GPT_GUID_LEN] = GUID_EMPTY_VALUE;
  static constexpr uint8_t kZirconType[GPT_GUID_LEN] = GUID_CROS_KERNEL_VALUE;
  static constexpr uint8_t kFvmType[GPT_GUID_LEN] = GUID_FVM_VALUE;
  static constexpr uint8_t kSysCfgGuid[GPT_GUID_LEN] = GUID_SYS_CONFIG_VALUE;

 protected:
  ChromebookX64AbrTests()
      : dispatcher_(&kAsyncLoopConfigNoAttachToCurrentThread),
        dispatcher2_(&kAsyncLoopConfigAttachToCurrentThread),
        fake_svc_(dispatcher_.dispatcher(), mock_boot_arguments::Server()) {
    IsolatedDevmgr::Args args;
    args.driver_search_paths.push_back("/boot/driver");
    args.disable_block_watcher = false;
    args.board_name = "chromebook-x64";

    ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr_));
    fbl::unique_fd fd;
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "sys/platform", &fd));
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "misc/ramctl", &fd));
    ASSERT_NO_FATAL_FAILURES(
        BlockDevice::Create(devmgr_.devfs_root(), kEmptyType, kDiskBlocks, kBlockSize, &disk_));
    fake_svc_.fake_boot_args().GetArgumentsMap().emplace("zvb.current_slot", "_a");
    dispatcher_.StartThread("abr-svc-test-loop");
    dispatcher2_.StartThread("abr-svc-test-loop-2");
  }

  ~ChromebookX64AbrTests() override { dispatcher_.Shutdown(); }

  void SetupPartitions(AbrSlotIndex active_slot) {
    std::unique_ptr<gpt::GptDevice> gpt;
    ASSERT_OK(gpt::GptDevice::Create(disk_->fd(), /*blocksize=*/disk_->block_size(),
                                     /*blocks=*/disk_->block_count(), &gpt));
    ASSERT_OK(gpt->Sync());
    // 2 (GPT header and MBR header) blocks + number of blocks in entry array.
    uint64_t cur_start = 2 + gpt->EntryArrayBlockCount();
    ASSERT_OK(gpt->AddPartition("zircon-a", kZirconType, kZirconType, cur_start, kZxPartBlocks, 0));
    cur_start += kZxPartBlocks;
    ASSERT_OK(gpt->AddPartition("zircon-b", kZirconType, kZirconType, cur_start, kZxPartBlocks, 0));
    cur_start += kZxPartBlocks;
    ASSERT_OK(gpt->AddPartition("zircon-r", kZirconType, kZirconType, cur_start, kZxPartBlocks, 0));
    cur_start += kZxPartBlocks;
    ASSERT_OK(gpt->AddPartition("fvm", kFvmType, kFvmType, cur_start, kMinFvmSize, 0));
    cur_start += kMinFvmSize;
    ASSERT_OK(gpt->AddPartition("syscfg", kSysCfgGuid, kSysCfgGuid, cur_start, kSysCfgSize, 0));

    int active_partition = -1;
    auto current_slot = "_x";
    switch (active_slot) {
      case kAbrSlotIndexA:
        active_partition = 0;
        current_slot = "_a";
        break;
      case kAbrSlotIndexB:
        active_partition = 1;
        current_slot = "_b";
        break;
      case kAbrSlotIndexR:
        active_partition = 2;
        current_slot = "_r";
        break;
    }

    auto result = gpt->GetPartition(active_partition);
    ASSERT_OK(result.status_value());
    gpt_partition_t* part = *result;
    gpt_cros_attr_set_priority(&part->flags, 15);
    gpt_cros_attr_set_successful(&part->flags, true);
    fake_svc_.fake_boot_args().GetArgumentsMap().emplace("zvb.current_slot", current_slot);
    ASSERT_OK(gpt->Sync());

    fdio_cpp::UnownedFdioCaller caller(disk_->fd());
    auto result2 = fidl::WireCall<fuchsia_device::Controller>(caller.channel())
                       .Rebind(fidl::StringView("/boot/driver/gpt.so"));
    ASSERT_TRUE(result2.ok());
    ASSERT_FALSE(result2->result.is_err());
  }

  zx::status<std::unique_ptr<abr::Client>> GetAbrClient() {
    auto& svc_root = fake_svc_.svc_chan();
    return paver::ChromebookX64AbrClientFactory().New(devmgr_.devfs_root().duplicate(), svc_root,
                                                      nullptr);
  }

  std::unique_ptr<BlockDevice> disk_;
  IsolatedDevmgr devmgr_;
  async::Loop dispatcher_;
  async::Loop dispatcher2_;
  FakeSvc<mock_boot_arguments::Server> fake_svc_;
};

TEST_F(ChromebookX64AbrTests, CreateSucceeds) {
  ASSERT_NO_FATAL_FAILURES(SetupPartitions(kAbrSlotIndexA));
  auto client = GetAbrClient();
  ASSERT_OK(client.status_value());
}

TEST_F(ChromebookX64AbrTests, QueryActiveSucceeds) {
  ASSERT_NO_FATAL_FAILURES(SetupPartitions(kAbrSlotIndexA));
  auto client = GetAbrClient();
  ASSERT_OK(client.status_value());

  bool marked_successful;
  AbrSlotIndex slot = client->GetBootSlot(false, &marked_successful);
  ASSERT_EQ(slot, kAbrSlotIndexA);
  ASSERT_TRUE(marked_successful);
}

TEST_F(ChromebookX64AbrTests, GetSlotInfoSucceeds) {
  ASSERT_NO_FATAL_FAILURES(SetupPartitions(kAbrSlotIndexB));
  auto client = GetAbrClient();
  ASSERT_OK(client.status_value());
  auto info = client->GetSlotInfo(kAbrSlotIndexB);
  ASSERT_OK(info.status_value());
  ASSERT_TRUE(info->is_active);
  ASSERT_TRUE(info->is_bootable);
  ASSERT_TRUE(info->is_marked_successful);
  ASSERT_EQ(info->num_tries_remaining, 0);
}
}  // namespace
