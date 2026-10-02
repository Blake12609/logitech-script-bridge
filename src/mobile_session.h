// The phone version of the bridge: the phone runs the script and drives a MAKCU
// (or similar device) through a USB OTG serial port. Nothing in here depends on
// Android, so it is tested on a PC; the app passes serial data in and out via JNI.
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "backend.h"
#include "engine.h"

// After "km.buttons(1)" a MAKCU reports the real mouse's buttons: one byte with a
// bit per button (bit 0 left, 1 right, 2 middle, 3 back, 4 forward) on every
// change, on its own or right after "km.". Text replies can be mixed in.
class MakcuButtonStream {
public:
    using ChangeFn = std::function<void(Button b, bool pressed)>;
    void feed(const char* data, size_t size, const ChangeFn& onChange);
    int mask() const { return mask_; }
    void reset();

private:
    void apply(int mask, const ChangeFn& onChange);
    std::string line_;  // text received since the last line break
    bool lastCR_ = false;
    int mask_ = 0;
};

// Numbers are shared with the app (NativeBridge.kt).
enum class MobileDevice { Makcu = 0, KmboxB = 1, Esp32 = 2, Demo = 3 };

struct MobileOptions {
    MobileDevice device = MobileDevice::Makcu;
    double jitterMin = 0, jitterMax = 0;
    int screenWidth = 1920, screenHeight = 1080;  // the PC's screen, for MoveMouseTo
};

class MobileSession {
public:
    using WriteFn = std::function<void(const std::string& bytes)>;
    using LogFn = std::function<void(const std::string& text)>;

    MobileSession(WriteFn write, LogFn log);
    ~MobileSession();

    // The device on the serial port; decides how received data is read.
    void setDevice(MobileDevice d);
    // Sent right after the serial port opens (MAKCU: start reporting the mouse buttons).
    static std::string connectCommands(MobileDevice d);

    bool start(const std::string& source, const std::string& chunkName, const MobileOptions& opt,
               std::string& error);
    void stop();
    bool running() const;

    void onSerialData(const char* data, size_t size);
    void onTouchButton(int n, bool pressed);  // on-screen mouse buttons, OnEvent numbering 1-5
    void onGKey(int n, bool pressed);

    // Bits 0-4: buttons 1-5 held on the mouse or screen. Bits 8-12: held by the script.
    int buttonMask();

    std::function<void()> onClearLog;

private:
    class Output;
    class Input;
    WriteFn write_;
    LogFn log_;
    std::atomic<MobileDevice> device_{MobileDevice::Makcu};
    std::unique_ptr<Output> out_;
    std::unique_ptr<Input> input_;
    std::unique_ptr<Engine> engine_;
    std::mutex streamMu_;
    MakcuButtonStream stream_;
};
