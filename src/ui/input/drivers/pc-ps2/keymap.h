// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_PC_PS2_KEYMAP_H_
#define SRC_UI_INPUT_DRIVERS_PC_PS2_KEYMAP_H_

#include <stdint.h>

#include <hid/usages.h>
namespace i8042 {

constexpr uint8_t kKeyUp = 0x80;
constexpr uint8_t kScancodeMask = 0x7f;

constexpr uint8_t kExtendedScancode = 0xe0;

inline constexpr uint8_t kSet1UsageMap[128] = {
    /* 0x00 */ 0,
    HID_USAGE_KEY_ESC,
    HID_USAGE_KEY_1,
    HID_USAGE_KEY_2,
    /* 0x04 */ HID_USAGE_KEY_3,
    HID_USAGE_KEY_4,
    HID_USAGE_KEY_5,
    HID_USAGE_KEY_6,
    /* 0x08 */ HID_USAGE_KEY_7,
    HID_USAGE_KEY_8,
    HID_USAGE_KEY_9,
    HID_USAGE_KEY_0,
    /* 0x0c */ HID_USAGE_KEY_MINUS,
    HID_USAGE_KEY_EQUAL,
    HID_USAGE_KEY_BACKSPACE,
    HID_USAGE_KEY_TAB,
    /* 0x10 */ HID_USAGE_KEY_Q,
    HID_USAGE_KEY_W,
    HID_USAGE_KEY_E,
    HID_USAGE_KEY_R,
    /* 0x14 */ HID_USAGE_KEY_T,
    HID_USAGE_KEY_Y,
    HID_USAGE_KEY_U,
    HID_USAGE_KEY_I,
    /* 0x18 */ HID_USAGE_KEY_O,
    HID_USAGE_KEY_P,
    HID_USAGE_KEY_LEFTBRACE,
    HID_USAGE_KEY_RIGHTBRACE,
    /* 0x1c */ HID_USAGE_KEY_ENTER,
    HID_USAGE_KEY_LEFT_CTRL,
    HID_USAGE_KEY_A,
    HID_USAGE_KEY_S,
    /* 0x20 */ HID_USAGE_KEY_D,
    HID_USAGE_KEY_F,
    HID_USAGE_KEY_G,
    HID_USAGE_KEY_H,
    /* 0x24 */ HID_USAGE_KEY_J,
    HID_USAGE_KEY_K,
    HID_USAGE_KEY_L,
    HID_USAGE_KEY_SEMICOLON,
    /* 0x28 */ HID_USAGE_KEY_APOSTROPHE,
    HID_USAGE_KEY_GRAVE,
    HID_USAGE_KEY_LEFT_SHIFT,
    HID_USAGE_KEY_BACKSLASH,
    /* 0x2c */ HID_USAGE_KEY_Z,
    HID_USAGE_KEY_X,
    HID_USAGE_KEY_C,
    HID_USAGE_KEY_V,
    /* 0x30 */ HID_USAGE_KEY_B,
    HID_USAGE_KEY_N,
    HID_USAGE_KEY_M,
    HID_USAGE_KEY_COMMA,
    /* 0x34 */ HID_USAGE_KEY_DOT,
    HID_USAGE_KEY_SLASH,
    HID_USAGE_KEY_RIGHT_SHIFT,
    HID_USAGE_KEY_KP_ASTERISK,
    /* 0x38 */ HID_USAGE_KEY_LEFT_ALT,
    HID_USAGE_KEY_SPACE,
    HID_USAGE_KEY_CAPSLOCK,
    HID_USAGE_KEY_F1,
    /* 0x3c */ HID_USAGE_KEY_F2,
    HID_USAGE_KEY_F3,
    HID_USAGE_KEY_F4,
    HID_USAGE_KEY_F5,
    /* 0x40 */ HID_USAGE_KEY_F6,
    HID_USAGE_KEY_F7,
    HID_USAGE_KEY_F8,
    HID_USAGE_KEY_F9,
    /* 0x44 */ HID_USAGE_KEY_F10,
    HID_USAGE_KEY_NUMLOCK,
    HID_USAGE_KEY_SCROLLLOCK,
    HID_USAGE_KEY_KP_7,
    /* 0x48 */ HID_USAGE_KEY_KP_8,
    HID_USAGE_KEY_KP_9,
    HID_USAGE_KEY_KP_MINUS,
    HID_USAGE_KEY_KP_4,
    /* 0x4c */ HID_USAGE_KEY_KP_5,
    HID_USAGE_KEY_KP_6,
    HID_USAGE_KEY_KP_PLUS,
    HID_USAGE_KEY_KP_1,
    /* 0x50 */ HID_USAGE_KEY_KP_2,
    HID_USAGE_KEY_KP_3,
    HID_USAGE_KEY_KP_0,
    HID_USAGE_KEY_KP_DOT,
    /* 0x54 */ 0,
    0,
    0,
    HID_USAGE_KEY_F11,
    /* 0x58 */ HID_USAGE_KEY_F12,
    0,
    0,
    0,
};

inline constexpr uint8_t kSet1ExtendedUsageMap[128] = {
    /* 0x00 */ 0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    /* 0x08 */ 0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    /* 0x10 */ 0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    /* 0x18 */ 0,
    0,
    0,
    0,
    HID_USAGE_KEY_KP_ENTER,
    HID_USAGE_KEY_RIGHT_CTRL,
    0,
    0,
    /* 0x20 */ 0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    /* 0x28 */ 0,
    0,
    0,
    0,
    0,
    0,
    HID_USAGE_KEY_VOL_DOWN,
    0,
    /* 0x30 */ HID_USAGE_KEY_VOL_UP,
    0,
    0,
    0,
    0,
    HID_USAGE_KEY_KP_SLASH,
    0,
    HID_USAGE_KEY_PRINTSCREEN,
    /* 0x38 */ HID_USAGE_KEY_RIGHT_ALT,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    /* 0x40 */ 0,
    0,
    0,
    0,
    0,
    0,
    0,
    HID_USAGE_KEY_HOME,
    /* 0x48 */ HID_USAGE_KEY_UP,
    HID_USAGE_KEY_PAGEUP,
    0,
    HID_USAGE_KEY_LEFT,
    0,
    HID_USAGE_KEY_RIGHT,
    0,
    HID_USAGE_KEY_END,
    /* 0x50 */ HID_USAGE_KEY_DOWN,
    HID_USAGE_KEY_PAGEDOWN,
    HID_USAGE_KEY_INSERT,
    HID_USAGE_KEY_DELETE,
    0,
    0,
    0,
    0,
    /* 0x58 */ 0,
    0,
    0,
    HID_USAGE_KEY_LEFT_GUI,
    HID_USAGE_KEY_RIGHT_GUI,
    0 /* MENU */,
    0,
    0,
};

}  // namespace i8042

#endif  // SRC_UI_INPUT_DRIVERS_PC_PS2_KEYMAP_H_
