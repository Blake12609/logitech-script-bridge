// Windows input: a low-level mouse hook that feeds real button presses to the
// engine, plus keyboard/cursor/screen state for the script API.
#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "engine.h"

class WinInput : public InputState {
public:
    ~WinInput() override { stopHook(); }
    // extraKeys: F13-F24 become "mouse" buttons 6-17, "gkeys" G1-G12, or "off".
    bool startHook(Engine* engine, const std::string& extraKeys, std::string& error);
    void stopHook();

    bool modifierPressed(const std::string& name) override;
    bool lockOn(const std::string& name) override;
    void cursorPos(int& x, int& y) override;
    void screenRect(bool virtualDesktop, int& left, int& top, int& width, int& height) override;

private:
    std::thread thread_;
    std::atomic<unsigned long> threadId_{0};
};
