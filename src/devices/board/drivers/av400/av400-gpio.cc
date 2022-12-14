// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/gpio.h>
#include <soc/aml-a5/a5-gpio.h>
#include <soc/aml-a5/a5-hw.h>

#include "av400.h"

namespace av400 {

static constexpr pbus_mmio_t gpio_mmios[] = {
    {
        .base = A5_GPIO_BASE,
        .length = A5_GPIO_LENGTH,
    },
    // A113X2 no AO mmio, as a placeholder
    {
        .base = A5_GPIO_BASE,
        .length = A5_GPIO_LENGTH,
    },
    {
        .base = A5_GPIO_INTERRUPT_BASE,
        .length = A5_GPIO_INTERRUPT_LENGTH,
    },
};

static constexpr pbus_irq_t gpio_irqs[] = {
    {
        .irq = A5_GPIO_IRQ_0,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = A5_GPIO_IRQ_1,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = A5_GPIO_IRQ_2,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = A5_GPIO_IRQ_3,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = A5_GPIO_IRQ_4,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = A5_GPIO_IRQ_5,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = A5_GPIO_IRQ_6,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = A5_GPIO_IRQ_7,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = A5_GPIO_IRQ_8,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = A5_GPIO_IRQ_9,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = A5_GPIO_IRQ_10,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = A5_GPIO_IRQ_11,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
};

// GPIOs to expose from generic GPIO driver.
static const gpio_pin_t gpio_pins[] = {
    DECL_GPIO_PIN(A5_GPIOB(12)), DECL_GPIO_PIN(A5_GPIOB(13)),    DECL_GPIO_PIN(A5_GPIOB(9)),
    DECL_GPIO_PIN(A5_GPIOT(10)), DECL_GPIO_PIN(A5_GPIOX(16)),    DECL_GPIO_PIN(A5_GPIOX(17)),
    DECL_GPIO_PIN(A5_GPIOX(6)),  DECL_GPIO_PIN(A5_ETH_MAC_INTR), DECL_GPIO_PIN(A5_GPIOD(9)),
    DECL_GPIO_PIN(A5_GPIOD(3)),
};

static const pbus_metadata_t gpio_metadata[] = {
    {
        .type = DEVICE_METADATA_GPIO_PINS,
        .data_buffer = reinterpret_cast<const uint8_t*>(&gpio_pins),
        .data_size = sizeof(gpio_pins),
    },
};

static pbus_dev_t gpio_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "gpio";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_A5;
  dev.did = PDEV_DID_AMLOGIC_GPIO;
  dev.mmio_list = gpio_mmios;
  dev.mmio_count = std::size(gpio_mmios);
  dev.irq_list = gpio_irqs;
  dev.irq_count = std::size(gpio_irqs);
  dev.metadata_list = gpio_metadata;
  dev.metadata_count = std::size(gpio_metadata);
  return dev;
}();

zx_status_t Av400::GpioInit() {
  zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_GPIO_IMPL, &gpio_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ProtocolDeviceAdd failed %s", __func__, zx_status_get_string(status));
    return status;
  }

  gpio_impl_ = ddk::GpioImplProtocolClient(parent());
  if (!gpio_impl_.is_valid()) {
    zxlogf(ERROR, "%s: device_get_protocol failed %d", __func__, status);
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

}  // namespace av400
