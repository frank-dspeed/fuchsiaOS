// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ADDING A NEW PROTOCOL
// When adding a new protocol, add a macro call at the end of this file after
// the last protocol definition with a tag, value, name, and flags in the form:
//
// DDK_PROTOCOL_DEF(tag, value, name, flags)
//
// The value must be a unique identifier that is just the previous protocol
// value plus 1.

// clang-format off

#ifndef DDK_PROTOCOL_DEF
#error Internal use only. Do not include.
#else
#ifndef PF_NOPUB
// Do not publish aliases in /dev/class/...
#define PF_NOPUB 1
#endif
DDK_PROTOCOL_DEF(BLOCK,                   1,    "block", 0)
DDK_PROTOCOL_DEF(BLOCK_IMPL,              2,    "block-impl", 0)
DDK_PROTOCOL_DEF(BLOCK_PARTITION,         3,    "block-partition", 0)
DDK_PROTOCOL_DEF(BLOCK_VOLUME,            4,    "block-volume", 0)
DDK_PROTOCOL_DEF(CODEC,                   6,    "codec", 0)
DDK_PROTOCOL_DEF(CONSOLE,                 8,    "console", 0)
DDK_PROTOCOL_DEF(DEVICE,                  9,    "device", 0)
DDK_PROTOCOL_DEF(DISPLAY_CAPTURE_IMPL,    10,   "display-capture-impl", PF_NOPUB)
DDK_PROTOCOL_DEF(DISPLAY_CONTROLLER,      11,   "display-controller", 0)
DDK_PROTOCOL_DEF(DISPLAY_CONTROLLER_IMPL, 12,   "display-controller-impl", PF_NOPUB)
DDK_PROTOCOL_DEF(DOTMATRIX_DISPLAY,       13,   "dotmatrix-display", 0)
DDK_PROTOCOL_DEF(ETHERNET,                14,   "ethernet", 0)
DDK_PROTOCOL_DEF(ETHERNET_IMPL,           15,   "ethernet-impl", 0)
DDK_PROTOCOL_DEF(FRAMEBUFFER,             16,   "framebuffer", 0)
DDK_PROTOCOL_DEF(GOLDFISH_ADDRESS_SPACE,  17,   "goldfish-address-space", 0)
DDK_PROTOCOL_DEF(GOLDFISH_CONTROL,        18,   "goldfish-control", 0)
DDK_PROTOCOL_DEF(GOLDFISH_PIPE,           19,   "goldfish-pipe", 0)
DDK_PROTOCOL_DEF(GPIO,                    20,   "gpio", PF_NOPUB)
DDK_PROTOCOL_DEF(GPIO_IMPL,               21,   "gpio-impl", PF_NOPUB)
DDK_PROTOCOL_DEF(HIDBUS,                  22,   "hidbus", 0)
DDK_PROTOCOL_DEF(HID_DEVICE,              23,   "input", 0)
DDK_PROTOCOL_DEF(I2C,                     24,   "i2c", 0)
DDK_PROTOCOL_DEF(I2C_IMPL ,               25,   "i2c-impl", 0)
DDK_PROTOCOL_DEF(INPUTREPORT,             26,   "input-report", 0)
DDK_PROTOCOL_DEF(ROOT,                    27,   "root", PF_NOPUB)
DDK_PROTOCOL_DEF(MISC,                    28,   "misc", PF_NOPUB)
DDK_PROTOCOL_DEF(ACPI,                    30,   "acpi", 0)
DDK_PROTOCOL_DEF(PCI,                     31,   "pci", 0)
DDK_PROTOCOL_DEF(PCIROOT,                 32,   "pci-root", PF_NOPUB)
DDK_PROTOCOL_DEF(USB,                     33,   "usb", PF_NOPUB)
DDK_PROTOCOL_DEF(USB_DEVICE,              34,   "usb-device", 0)
DDK_PROTOCOL_DEF(USB_BUS,                 35,   "usb-bus", PF_NOPUB)
DDK_PROTOCOL_DEF(USB_COMPOSITE,           36,   "usb-composite", PF_NOPUB)
DDK_PROTOCOL_DEF(USB_DCI,                 37,   "usb-dci", 0)  // Device Controller Interface
DDK_PROTOCOL_DEF(USB_INTERFACE,           38,   "usb-interface", PF_NOPUB)
DDK_PROTOCOL_DEF(USB_PERIPHERAL,          39,   "usb-peripheral", 0)
DDK_PROTOCOL_DEF(USB_FUNCTION,            40,   "usb-function", 0)
DDK_PROTOCOL_DEF(CACHE_TEST,              41,   "usb-cache-test", 0)
DDK_PROTOCOL_DEF(USB_HCI,                 42,   "usb-hci", 0)  // Host Controller Interface
DDK_PROTOCOL_DEF(USB_MODE_SWITCH,         43,   "usb-mode-switch", 0)
DDK_PROTOCOL_DEF(USB_DBC,                 44,   "usb-dbc", 0) // Debug Capability
DDK_PROTOCOL_DEF(USB_TESTER,              45,   "usb-tester", 0)
DDK_PROTOCOL_DEF(USB_FWLOADER,            46,   "usb-fwloader", 0)
DDK_PROTOCOL_DEF(BT_HCI,                  47,   "bt-hci", 0)
DDK_PROTOCOL_DEF(BT_EMULATOR,             48,   "bt-emulator", 0)  // Bluetooth hardware emulator interface
DDK_PROTOCOL_DEF(BT_TRANSPORT,            49,   "bt-transport", 0)
DDK_PROTOCOL_DEF(BT_HOST,                 50,   "bt-host", 0)
DDK_PROTOCOL_DEF(BT_GATT_SVC,             51,   "bt-gatt-svc", 0)
DDK_PROTOCOL_DEF(AUDIO,                   52,   "audio", 0)
DDK_PROTOCOL_DEF(MIDI,                    53,   "midi", 0)
DDK_PROTOCOL_DEF(SDHCI,                   54,   "sdhci", 0)
DDK_PROTOCOL_DEF(SDMMC,                   55,   "sdmmc", 0)
DDK_PROTOCOL_DEF(SDIO,                    56,   "sdio", 0)
DDK_PROTOCOL_DEF(WLANPHY,                 57,   "wlanphy", 0)
DDK_PROTOCOL_DEF(WLANPHY_IMPL,            58,   "wlanphy-impl", 0)
DDK_PROTOCOL_DEF(WLANIF,                  59,   "wlanif", 0)
DDK_PROTOCOL_DEF(WLANIF_IMPL,             60,   "wlanif-impl", 0)
DDK_PROTOCOL_DEF(WLANMAC,                 61,   "wlanmac", 0)
DDK_PROTOCOL_DEF(CAMERA,                  64,   "camera", 0)
DDK_PROTOCOL_DEF(CAMERA_SENSOR,           65,   "camera-sensor", PF_NOPUB)
DDK_PROTOCOL_DEF(ISP,                     66,   "isp", 0)
DDK_PROTOCOL_DEF(CAMERA_SENSOR2,          67,   "camera-sensor2", PF_NOPUB)  // RESERVED
DDK_PROTOCOL_DEF(VCAM_FACTORY,            68,   "virtual-camera-factory", 0)
DDK_PROTOCOL_DEF(OUTPUT_STREAM,           69,   "output-stream", PF_NOPUB)
DDK_PROTOCOL_DEF(MEDIA_CODEC,             70,   "media-codec", 0)
DDK_PROTOCOL_DEF(BATTERY,                 71,   "battery", 0)
DDK_PROTOCOL_DEF(POWER,                   72,   "power", 0)
DDK_PROTOCOL_DEF(THERMAL,                 73,   "thermal", 0)
DDK_PROTOCOL_DEF(GPU_THERMAL,             74,   "gpu-thermal", 0)
DDK_PROTOCOL_DEF(PTY,                     75,   "pty", 0)
DDK_PROTOCOL_DEF(IHDA,                    76,   "intel-hda", 0)
DDK_PROTOCOL_DEF(IHDA_CODEC,              77,   "intel-hda-codec", 0)
DDK_PROTOCOL_DEF(IHDA_DSP,                78,   "intel-hda-dsp", 0)
DDK_PROTOCOL_DEF(TEST,                    80,   "test", 0)
DDK_PROTOCOL_DEF(TEST_COMPAT_CHILD,       81,   "test-compat-child", 0)
DDK_PROTOCOL_DEF(TEST_POWER_CHILD,        82,   "test-power-child", 0)
DDK_PROTOCOL_DEF(TEST_PARENT,             83,   "test-parent", PF_NOPUB)
DDK_PROTOCOL_DEF(PBUS,                    84,   "platform-bus", 0)
DDK_PROTOCOL_DEF(PDEV,                    85,   "platform-dev", 0)
DDK_PROTOCOL_DEF(I2C_HID,                 86,   "i2c-hid", 0)
DDK_PROTOCOL_DEF(SERIAL,                  87,   "serial", 0)
DDK_PROTOCOL_DEF(SERIAL_IMPL,             88,   "serial-impl", 0)
DDK_PROTOCOL_DEF(SHARED_DMA,              89,   "shared-dma", 0)
DDK_PROTOCOL_DEF(CLOCK,                   90,   "clock", PF_NOPUB)
DDK_PROTOCOL_DEF(CLOCK_IMPL,              91,   "clock-impl", 0)
DDK_PROTOCOL_DEF(INTEL_GPU_CORE,          92,   "intel-gpu-core", 0)
DDK_PROTOCOL_DEF(IOMMU,                   93,   "iommu", 0)
DDK_PROTOCOL_DEF(NAND,                    94,   "nand", 0)
DDK_PROTOCOL_DEF(RAW_NAND,                95,   "rawnand", 0)
DDK_PROTOCOL_DEF(BAD_BLOCK,               96,   "bad-block", PF_NOPUB)
DDK_PROTOCOL_DEF(MAILBOX,                 97,   "mailbox", PF_NOPUB)
DDK_PROTOCOL_DEF(SCPI,                    98,   "scpi", PF_NOPUB)
DDK_PROTOCOL_DEF(BACKLIGHT,               99,   "backlight", 0)
DDK_PROTOCOL_DEF(AMLOGIC_CANVAS,          100,  "aml-canvas", PF_NOPUB)
DDK_PROTOCOL_DEF(SKIP_BLOCK,              101,  "skip-block", 0)
DDK_PROTOCOL_DEF(ETH_BOARD,               102,  "ethernet-board", PF_NOPUB)
DDK_PROTOCOL_DEF(ETH_MAC,                 103,  "ethernet-mac", PF_NOPUB)
DDK_PROTOCOL_DEF(QMI_TRANSPORT,           104,  "qmi-transport", 0)
DDK_PROTOCOL_DEF(MIPI_CSI,                105,  "mipi-csi", PF_NOPUB)
DDK_PROTOCOL_DEF(GDC,                     106,  "gdc", PF_NOPUB)
DDK_PROTOCOL_DEF(GE2D,                    107,  "ge2d", PF_NOPUB)
DDK_PROTOCOL_DEF(LIGHT,                   108,  "light", 0)
DDK_PROTOCOL_DEF(DSI_IMPL,                109,  "dsi-impl", PF_NOPUB)
DDK_PROTOCOL_DEF(POWER_IMPL,              110,  "power-impl", PF_NOPUB)
DDK_PROTOCOL_DEF(USB_PHY,                 111,  "usb-phy", 0)
DDK_PROTOCOL_DEF(NNA,                     113,  "nna", PF_NOPUB)
// Protocol definition at garnet/magma/src/magma_util/platform/zircon/zircon_platform_ioctl.h
DDK_PROTOCOL_DEF(GPU,                     114, "gpu", 0)
DDK_PROTOCOL_DEF(RTC,                     115, "rtc", 0)
DDK_PROTOCOL_DEF(TEE,                     116, "tee", 0)
DDK_PROTOCOL_DEF(VSOCK,                   117, "vsock", 0)
DDK_PROTOCOL_DEF(SYSMEM,                  118, "sysmem", 0)
DDK_PROTOCOL_DEF(MLG,                     119, "mlg", 0)
DDK_PROTOCOL_DEF(ZXCRYPT,                 120, "zxcrypt", 0)
DDK_PROTOCOL_DEF(SPI,                     121, "spi", 0)
DDK_PROTOCOL_DEF(SPI_IMPL,                122, "spi-impl", 0)
DDK_PROTOCOL_DEF(SECURE_MEM,              123, "securemem", 0)
DDK_PROTOCOL_DEF(DEVHOST_TEST,            124, "tdh", 0)
DDK_PROTOCOL_DEF(SERIAL_IMPL_ASYNC,       125, "serial-impl-async", 0)
DDK_PROTOCOL_DEF(AT_TRANSPORT,            126, "at-transport", 0)
DDK_PROTOCOL_DEF(PWM,                     127, "pwm", 0)
DDK_PROTOCOL_DEF(PWM_IMPL,                128, "pwm-impl", 0)
DDK_PROTOCOL_DEF(CPU_CTRL,                129, "cpu-ctrl", 0)
DDK_PROTOCOL_DEF(NETWORK_DEVICE,          130, "network", 0)
DDK_PROTOCOL_DEF(NETWORK_DEVICE_IMPL,     131, "network-impl", PF_NOPUB)
DDK_PROTOCOL_DEF(MAC_ADDR_IMPL,           132, "network-mac-impl", PF_NOPUB)
DDK_PROTOCOL_DEF(OT_RADIO,                133, "ot-radio", 0)
DDK_PROTOCOL_DEF(INPUTREPORT_INJECT,      134, "input-report-inject", 0)
DDK_PROTOCOL_DEF(USB_HCI_TEST,            135, "usb-hci-test", 0)
DDK_PROTOCOL_DEF(ACPI_DEVICE,             136, "acpi-device", 0)
DDK_PROTOCOL_DEF(VIRTUALBUS_TEST,         137, "virtual-bus-test", 0)
DDK_PROTOCOL_DEF(TEST_ASIX_FUNCTION,      138, "test-asix-function", 0)
DDK_PROTOCOL_DEF(RPMB,                    139, "rpmb", 0)
DDK_PROTOCOL_DEF(AUDIO_INPUT,             140, "audio-input", 0)
DDK_PROTOCOL_DEF(AUDIO_OUTPUT,            141, "audio-output", 0)
DDK_PROTOCOL_DEF(AMLOGIC_RAM,             142, "aml-ram", 0)
DDK_PROTOCOL_DEF(GPU_PERFORMANCE_COUNTERS, 143, "gpu-performance-counters", 0)
DDK_PROTOCOL_DEF(DISPLAY_CLAMP_RGB_IMPL,  144, "display-clamprgb-impl", PF_NOPUB)
DDK_PROTOCOL_DEF(TEMPERATURE,             145, "temperature", 0)
DDK_PROTOCOL_DEF(VREG,                    146, "vreg", 0)
DDK_PROTOCOL_DEF(ADC,                     147, "adc", 0)
DDK_PROTOCOL_DEF(DSI,                     148, "dsi", PF_NOPUB)
DDK_PROTOCOL_DEF(BT_VENDOR,               149, "bt-vendor", 0)
DDK_PROTOCOL_DEF(DSI_BASE,                150, "dsi-base", 0)
DDK_PROTOCOL_DEF(POWER_SENSOR,            151, "power-sensor", 0)
DDK_PROTOCOL_DEF(REGISTERS,               152, "registers", 0)
DDK_PROTOCOL_DEF(DAI,                     153, "dai", 0)
DDK_PROTOCOL_DEF(GOLDFISH_SYNC,           154, "goldfish-sync", 0)
DDK_PROTOCOL_DEF(RADAR,                   155, "radar", 0)
DDK_PROTOCOL_DEF(ARM_MALI,                156, "arm-mali", PF_NOPUB)
DDK_PROTOCOL_DEF(HDMI,                    157, "hdmi", PF_NOPUB)
DDK_PROTOCOL_DEF(GPU_DEPENDENCY_INJECTION, 158, "gpu-dependency-injection", 0)
DDK_PROTOCOL_DEF(TPM_IMPL,                159, "tpm-impl", 0)
DDK_PROTOCOL_DEF(TPM,                     160, "tpm", 0)
DDK_PROTOCOL_DEF(CHROMEOS_ACPI,           161, "chromeos-acpi", 0)
#undef DDK_PROTOCOL_DEF
#endif
