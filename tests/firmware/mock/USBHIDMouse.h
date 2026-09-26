// ESP32 Arduino core: USBHIDMouse.h (mock, same signatures and constants)
#pragma once
#include "Arduino.h"

#define MOUSE_LEFT 0x01
#define MOUSE_RIGHT 0x02
#define MOUSE_MIDDLE 0x04
#define MOUSE_BACKWARD 0x08
#define MOUSE_FORWARD 0x10
#define MOUSE_ALL 0x1F

class USBHIDMouse {
public:
    void begin(void) {}
    void end(void) {}
    void move(int8_t x, int8_t y, int8_t wheel = 0, int8_t pan = 0) {
        (void)pan;
        hidLog("move %d %d %d", x, y, wheel);
    }
    void press(uint8_t b = MOUSE_LEFT) { hidLog("press 0x%02X", b); }
    void release(uint8_t b = MOUSE_LEFT) { hidLog("release 0x%02X", b); }
};
