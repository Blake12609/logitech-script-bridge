// Arduino Keyboard library (mock, same signatures)
#pragma once
#include "Arduino.h"

#define KEY_LEFT_CTRL 0x80
#define KEY_LEFT_SHIFT 0x81
#define KEY_LEFT_ALT 0x82
#define KEY_LEFT_GUI 0x83

class Keyboard_ {
public:
    void begin(void) {}
    void end(void) {}
    size_t press(uint8_t k) {
        hidLog("kpress %d", k);
        return 1;
    }
    size_t release(uint8_t k) {
        hidLog("krelease %d", k);
        return 1;
    }
    void releaseAll(void) {}
};
inline Keyboard_ Keyboard;
