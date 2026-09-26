// ESP32 Arduino core: USB.h (mock)
#pragma once
#include "Arduino.h"

class ESPUSB {
public:
    bool begin() { return true; }
};
inline ESPUSB USB;
