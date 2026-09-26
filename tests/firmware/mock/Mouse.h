// Arduino Mouse library (mock, same signatures and constants)
#pragma once
#include "Arduino.h"

#define MOUSE_LEFT 1
#define MOUSE_RIGHT 2
#define MOUSE_MIDDLE 4
#define MOUSE_ALL (MOUSE_LEFT | MOUSE_RIGHT | MOUSE_MIDDLE)

class Mouse_ {
public:
    void begin(void) {}
    void end(void) {}
    void move(signed char x, signed char y, signed char wheel = 0) { hidLog("move %d %d %d", x, y, wheel); }
    void press(uint8_t b = MOUSE_LEFT) { hidLog("press %d", b); }
    void release(uint8_t b = MOUSE_LEFT) { hidLog("release %d", b); }
};
inline Mouse_ Mouse;
