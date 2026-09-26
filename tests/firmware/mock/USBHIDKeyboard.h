// ESP32 Arduino core: USBHIDKeyboard.h (mock, same signatures)
#pragma once
#include "Arduino.h"

class USBHIDKeyboard {
public:
    void begin(void) {}
    void end(void) {}
    size_t pressRaw(uint8_t k) {
        hidLog("keydown 0x%02X", k);
        return 1;
    }
    size_t releaseRaw(uint8_t k) {
        hidLog("keyup 0x%02X", k);
        return 1;
    }
    void releaseAll(void) {}
};
