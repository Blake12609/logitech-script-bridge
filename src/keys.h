// Logitech G-series key names <-> set-1 scancodes <-> USB HID usage IDs.
//
// Scripts name keys ("lshift", "a", "f5") or give set-1 scancodes (0x2A,
// 0x1E, 0x3F). Hardware needs HID usage IDs, SendInput wants scancodes.
#pragma once

#include <string>

struct KeyInfo {
    const char* name;
    int scancode;  // Logitech/set-1 scancode; 0x1xx = extended key
    int hid;       // USB HID usage ID
};

// Returns nullptr if unknown. Names are case-insensitive and accept a few aliases.
const KeyInfo* findKeyByName(const std::string& name);
const KeyInfo* findKeyByScancode(int scancode);
