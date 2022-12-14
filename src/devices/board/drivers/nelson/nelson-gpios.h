// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_NELSON_NELSON_GPIOS_H_
#define SRC_DEVICES_BOARD_DRIVERS_NELSON_NELSON_GPIOS_H_

#include <soc/aml-s905d3/s905d3-gpio.h>

namespace nelson {

// clang-format off

// GPIO A Bank
#define GPIO_INRUSH_EN_SOC      S905D3_GPIOA(0)
#define GPIO_SOC_I2S_SCLK       S905D3_GPIOA(1)
#define GPIO_SOC_I2S_FS         S905D3_GPIOA(2)
#define GPIO_SOC_I2S_DO0        S905D3_GPIOA(3)
#define GPIO_SOC_I2S_DIN0       S905D3_GPIOA(4)
#define GPIO_SOC_AUDIO_EN       S905D3_GPIOA(5)
// GPIOA(6)
#define GPIO_SOC_MIC_DCLK       S905D3_GPIOA(7)
#define GPIO_SOC_MICLR_DIN0     S905D3_GPIOA(8)
#define GPIO_SOC_MICLR_DIN1     S905D3_GPIOA(9)
#define GPIO_SOC_BKL_EN         S905D3_GPIOA(10)
// GPIOA(11)
#define GPIO_AUDIO_SOC_FAULT_L  S905D3_GPIOA(12)
#define GPIO_SOC_TH_RST_L       S905D3_GPIOA(13)
#define GPIO_SOC_AV_I2C_SDA     S905D3_GPIOA(14)
#define GPIO_SOC_AV_I2C_SCL     S905D3_GPIOA(15)

// GPIO Z Bank
#define GPIO_HW_ID_3            S905D3_GPIOZ(0)
#define GPIO_SOC_TH_BOOT_MODE_L S905D3_GPIOZ(1)
#define GPIO_MUTE_SOC           S905D3_GPIOZ(2)
#define GPIO_HW_ID_2            S905D3_GPIOZ(3)
#define GPIO_TOUCH_SOC_INT_L    S905D3_GPIOZ(4)
#define GPIO_VOL_UP_L           S905D3_GPIOZ(5)
#define GPIO_VOL_DN_L           S905D3_GPIOZ(6)
#define GPIO_HW_ID_0            S905D3_GPIOZ(7)
#define GPIO_HW_ID_1            S905D3_GPIOZ(8)
#define GPIO_SOC_TOUCH_RST_L    S905D3_GPIOZ(9)
#define GPIO_ALERT_PWR_L        S905D3_GPIOZ(10)
#define GPIO_DISP_SOC_ID0       S905D3_GPIOZ(11)
#define GPIO_DISP_SOC_ID1       S905D3_GPIOZ(12)
#define GPIO_SOC_DISP_RST_L     S905D3_GPIOZ(13)
#define GPIO_SOC_TOUCH_I2C_SDA  S905D3_GPIOZ(14)
#define GPIO_SOC_TOUCH_I2C_SCL  S905D3_GPIOZ(15)

// GPIO C Bank
#define GPIO_SOC_SPI_A_MOSI     S905D3_GPIOC(0)
#define GPIO_SOC_SPI_A_MISO     S905D3_GPIOC(1)
#define GPIO_SOC_SPI_A_SS0      S905D3_GPIOC(2)
#define GPIO_SOC_SPI_A_SCLK     S905D3_GPIOC(3)
// GPIOC(4)
#define GPIO_TH_SOC_INT         S905D3_GPIOC(5)
// GPIOC(6)
#define GPIO_SOC_TH_INT         S905D3_GPIOC(7)

// GPIO X Bank
#define GPIO_SOC_WIFI_SDIO_D0    S905D3_GPIOX(0)
#define GPIO_SOC_WIFI_SDIO_D1    S905D3_GPIOX(1)
#define GPIO_SOC_WIFI_SDIO_D2    S905D3_GPIOX(2)
#define GPIO_SOC_WIFI_SDIO_D3    S905D3_GPIOX(3)
#define GPIO_SOC_WIFI_SDIO_CLK   S905D3_GPIOX(4)
#define GPIO_SOC_WIFI_SDIO_CMD   S905D3_GPIOX(5)
#define GPIO_SOC_WIFI_REG_ON     S905D3_GPIOX(6)
#define GPIO_WIFI_SOC_WAKE       S905D3_GPIOX(7)
#define GPIO_SOC_BT_PCM_IN       S905D3_GPIOX(8)
#define GPIO_SOC_BT_PCM_OUT      S905D3_GPIOX(9)
#define GPIO_SOC_BT_PCM_SYNC     S905D3_GPIOX(10)
#define GPIO_SOC_BT_PCM_CLK      S905D3_GPIOX(11)
#define GPIO_SOC_BT_UART_TX      S905D3_GPIOX(12)
#define GPIO_SOC_BT_UART_RX      S905D3_GPIOX(13)
#define GPIO_SOC_BT_UART_CTS     S905D3_GPIOX(14)
#define GPIO_SOC_BT_UART_RTS     S905D3_GPIOX(15)
#define GPIO_SOC_WIFI_LPO_32K768 S905D3_GPIOX(16)
#define GPIO_SOC_BT_REG_ON       S905D3_GPIOX(17)
#define GPIO_BT_SOC_WAKE         S905D3_GPIOX(18)
#define GPIO_SOC_BT_WAKE         S905D3_GPIOX(19)

// GPIO H Bank
// GPIOH(0)
// GPIOH(1)
#define GPIO_SOC_SELINA_RESET   S905D3_GPIOH(2)
#define GPIO_SOC_SELINA_IRQ_OUT S905D3_GPIOH(3)
#define GPIO_SOC_SPI_B_MOSI     S905D3_GPIOH(4)
#define GPIO_SOC_SPI_B_MISO     S905D3_GPIOH(5)
#define GPIO_SOC_SPI_B_SS0      S905D3_GPIOH(6)
#define GPIO_SOC_SPI_B_SCLK     S905D3_GPIOH(7)
#define GPIO_SOC_SELINA_OSC_EN  S905D3_GPIOH(8)

// GPIO AO Banks
#define GPIO_SOC_DEBUG_UARTAO_TX S905D3_GPIOAO(0)
#define GPIO_SOC_DEBUG_UARTAO_RX S905D3_GPIOAO(1)
#define GPIO_SOC_SENSORS_I2C_SCL S905D3_GPIOAO(2)
#define GPIO_SOC_SENSORS_I2C_SDA S905D3_GPIOAO(3)
#define GPIO_HW_ID_4             S905D3_GPIOAO(4)
#define GPIO_RGB_SOC_INT_L       S905D3_GPIOAO(5)
#define GPIO_SOC_JTAG_TCK        S905D3_GPIOAO(6)
#define GPIO_SOC_JTAG_TMS        S905D3_GPIOAO(7)
#define GPIO_SOC_JTAG_TDI        S905D3_GPIOAO(8)
#define GPIO_SOC_JTAG_TDO        S905D3_GPIOAO(9)
#define GPIO_FDR_L               S905D3_GPIOAO(10)
#define GPIO_AMBER_LED_PWM       S905D3_GPIOAO(11)

// GPIOE Banks
#define GPIO_SOC_VDDEE_PWM       S905D3_GPIOE(0)
#define GPIO_SOC_VDDCPU_PWM      S905D3_GPIOE(1)

// GPIOBOOT Banks
#define SOC_EMMC_D0              S905D3_GPIOBOOT(0)
#define SOC_EMMC_D1              S905D3_GPIOBOOT(1)
#define SOC_EMMC_D2              S905D3_GPIOBOOT(2)
#define SOC_EMMC_D3              S905D3_GPIOBOOT(3)
#define SOC_EMMC_D4              S905D3_GPIOBOOT(4)
#define SOC_EMMC_D5              S905D3_GPIOBOOT(5)
#define SOC_EMMC_D6              S905D3_GPIOBOOT(6)
#define SOC_EMMC_D7              S905D3_GPIOBOOT(7)
#define SOC_EMMC_CLK             S905D3_GPIOBOOT(8)
// GPIOBOOT(9)
#define SOC_EMMC_CMD             S905D3_GPIOBOOT(10)
// GPIOBOOT(11)
#define SOC_EMMC_RST_L           S905D3_GPIOBOOT(12)
#define SOC_EMMC_DS              S905D3_GPIOBOOT(13)
// GPIOBOOT(14)
// GPIOBOOT(15)

// clang-format on

}  // namespace nelson

#endif  // SRC_DEVICES_BOARD_DRIVERS_NELSON_NELSON_GPIOS_H_
