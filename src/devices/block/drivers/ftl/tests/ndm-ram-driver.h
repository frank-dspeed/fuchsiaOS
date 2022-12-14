// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_FTL_TESTS_NDM_RAM_DRIVER_H_
#define SRC_DEVICES_BLOCK_DRIVERS_FTL_TESTS_NDM_RAM_DRIVER_H_

#include <inttypes.h>
#include <zircon/types.h>

#include <cstdint>
#include <functional>
#include <limits>

#include <fbl/array.h>

#include "src/storage/lib/ftl/ftln/ndm-driver.h"

struct TestOptions {
  static constexpr TestOptions NoEccErrors() {
    TestOptions options = {};
    options.ecc_error_interval = std::numeric_limits<int>::max();
    options.save_config_data = false;
    return options;
  }

  // Controls simulation of ECC errors.
  int ecc_error_interval = 900;

  // Controls simulation of bad block sequence.
  int bad_block_interval = 50;

  // Controls the size of the sequence of operations that will run
  // into a bad block.
  int bad_block_burst = 1;

  // Makes only half of the space visible.
  bool use_half_size = false;

  // Save options on the partition info.
  bool save_config_data = true;

  // Delay before power failure kicks in.
  int power_failure_delay = -1;

  // Any write that triggers a power cut will write only the first half of the page and spare.
  bool emulate_half_write_on_power_failure = false;

  // Override logger passed to FTL via the NDMBaseDriver.
  std::optional<FtlLogger> ftl_logger;
};

// Ram-backed driver for testing purposes.
class NdmRamDriver final : public ftl::NdmBaseDriver {
 public:
  enum Op {
    Read,
    Write,
    Erase,
  };

  explicit NdmRamDriver(const ftl::VolumeOptions& options) : NdmRamDriver(options, {}) {}
  NdmRamDriver(const ftl::VolumeOptions& options, const TestOptions& test_options)
      : NdmBaseDriver(test_options.ftl_logger.value_or(ftl::DefaultLogger())),
        options_(options),
        test_options_(test_options) {}
  ~NdmRamDriver() final = default;

  // Extends the visible volume to the whole size of the storage.
  bool DoubleSize();

  void save_config_data(bool value) { test_options_.save_config_data = value; }

  void set_options(const ftl::VolumeOptions& options) { options_ = options; }
  void set_max_bad_blocks(uint32_t value) { options_.max_bad_blocks = value; }
  uint32_t num_bad_blocks() const { return num_bad_blocks_; }

  void SetPowerFailureDelay(int delay) {
    test_options_.power_failure_delay = delay;
    power_failure_delay_ = 0;
    power_failure_triggered_ = false;
  }

  // Set a callback to be run on every individual page read, write, and erase. If the callback
  // returns a non-zero value, that will be returned for the operation with no action performed.
  void set_operation_callback(std::function<int(Op, uint32_t)> operation_callback) {
    operation_callback_ = std::move(operation_callback);
  }

  // Access flags for a given page.
  bool FailEcc(uint32_t page_num) const;
  bool UnsafeEcc(uint32_t page_num) const;
  bool BadBlock(uint32_t page_num) const;
  void SetFailEcc(uint32_t page_num, bool value);
  void SetUnsafeEcc(uint32_t page_num, bool value);
  void SetBadBlock(uint32_t page_num, bool value);

  // Access the main data and spare area for a given page.
  uint8_t* MainData(uint32_t page_num);
  uint8_t* SpareData(uint32_t page_num);

  // NdmDriver interface:
  const char* Init() final;
  const char* Attach(const ftl::Volume* ftl_volume) final;
  bool Detach() final;
  int NandRead(uint32_t start_page, uint32_t page_count, void* page_buffer, void* oob_buffer) final;
  int NandWrite(uint32_t start_page, uint32_t page_count, const void* page_buffer,
                const void* oob_buffer) final;
  int NandErase(uint32_t page_num) final;
  int IsBadBlock(uint32_t page_num) final;
  bool IsEmptyPage(uint32_t page_num, const uint8_t* data, const uint8_t* spare) const final;
  uint32_t PageSize() const final;
  uint8_t SpareSize() const final;

 private:
  // Reads or Writes a single page.
  int ReadPage(uint32_t page_num, uint8_t* data, uint8_t* spare);
  int WritePage(uint32_t page_num, const uint8_t* data, const uint8_t* spare);

  // Returns true for a freshly minted bad block.
  bool SimulateBadBlock(uint32_t page_num);

  // Returns true if a power failure should be triggered at this point.
  bool ShouldTriggerPowerFailure();

  // Triggers side-effects of a power failure.
  void OnWritePowerFailure(uint64_t page_number, const uint8_t* data, const uint8_t* spare);
  void OnErasePowerFailure(uint64_t page_number);

  // Access flags for a given page.
  bool Written(uint32_t page_num) const;
  void SetWritten(uint32_t page_num, bool value);

  uint32_t PagesPerBlock() const;

  fbl::Array<uint8_t> volume_;
  fbl::Array<uint8_t> flags_;
  ftl::VolumeOptions options_;
  TestOptions test_options_;

  // Controls simulation of ECC errors.
  int ecc_error_interval_ = 0;

  // Controls simulation of bad blocks.
  int bad_block_interval_ = 0;

  // Marks that power failure happened.
  bool power_failure_triggered_ = false;

  // Controls simulation of power failures.
  int power_failure_delay_ = 0;

  uint32_t num_bad_blocks_ = 0;

  std::function<int(Op, uint32_t)> operation_callback_;
};

#endif  // SRC_DEVICES_BLOCK_DRIVERS_FTL_TESTS_NDM_RAM_DRIVER_H_
