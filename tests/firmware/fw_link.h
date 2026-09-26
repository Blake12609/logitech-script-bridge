// End-to-end harness: a Lua script runs in the real Engine, the app's km/kb
// protocol lines are fed straight into the firmware sketch under test, and
// the mocks record the USB HID actions the firmware would send to the PC.
#pragma once

#include <string>
#include <vector>

#include "backend.h"
#include "engine.h"
#include "test.h"

void setup();
void loop();

class FirmwareLink : public KmBackend {
public:
    FirmwareLink() : KmBackend(true) {}

protected:
    void sendLine(const std::string& line) override {
        Serial.feed(line + "\r\n");
        loop();
    }
};

class NoInput : public InputState {
public:
    bool modifierPressed(const std::string&) override { return false; }
    bool lockOn(const std::string&) override { return false; }
    void cursorPos(int& x, int& y) override { x = y = 0; }
    void screenRect(bool, int& l, int& t, int& w, int& h) override { l = t = 0; w = 1920; h = 1080; }
};

inline std::vector<std::string> runScriptThroughFirmware(const std::string& lua, std::string* log = nullptr) {
    g_hid.clear();
    FirmwareLink link;
    NoInput input;
    std::string text, err;
    Engine engine(link, input, [&](const std::string& s) { text += s; });
    if (!engine.start(lua, "fw.lua", err)) return {"load error: " + err};
    test::sleepMs(150);
    engine.stop();
    if (log) *log = text;
    return g_hid;
}

// Feed raw text to the sketch and return what it did.
inline std::vector<std::string> feed(const std::string& text) {
    g_hid.clear();
    Serial.feed(text);
    loop();
    return g_hid;
}

using Hid = std::vector<std::string>;
