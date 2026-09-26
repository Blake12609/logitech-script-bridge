// Output devices: where a script's mouse/keyboard actions end up.
#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "keys.h"

enum class Button { Left, Right, Middle, Side1, Side2 };
constexpr int kButtonCount = 5;
const char* buttonName(Button b);  // "left", "right", "middle", "side1", "side2"

class Backend {
public:
    virtual ~Backend() = default;
    virtual bool open(std::string& error) { (void)error; return true; }
    virtual void close() {}
    virtual bool supportsKeyboard() const { return false; }
    virtual void move(int dx, int dy) = 0;
    virtual void wheel(int clicks) = 0;
    virtual void button(Button b, bool down) = 0;
    virtual void key(const KeyInfo& k, bool down) { (void)k; (void)down; }
};

// Records what would have been sent. Used for testing scripts and in unit tests.
class DryRunBackend : public Backend {
public:
    explicit DryRunBackend(bool keyboard = true) : keyboard_(keyboard) {}
    bool supportsKeyboard() const override { return keyboard_; }
    void move(int dx, int dy) override;
    void wheel(int clicks) override;
    void button(Button b, bool down) override;
    void key(const KeyInfo& k, bool down) override;

    std::vector<std::string> sent() const;
    void setLogger(void (*fn)(void*, const std::string&), void* ctx) { logFn_ = fn; logCtx_ = ctx; }

private:
    void emit(const std::string& s);
    bool keyboard_;
    mutable std::mutex mu_;
    std::vector<std::string> sent_;
    void (*logFn_)(void*, const std::string&) = nullptr;
    void* logCtx_ = nullptr;
};

// Minimal cross-platform serial port.
class SerialPort {
public:
    ~SerialPort() { close(); }
    bool open(const std::string& port, int baud, std::string& error);
    bool setBaud(int baud);
    void close();
    bool write(const std::string& data);
    bool isOpen() const;
    int read(char* buf, int size);  // waits briefly; returns bytes read, <0 on error

private:
#ifdef _WIN32
    void* handle_ = nullptr;
#else
    int fd_ = -1;
#endif
};

std::vector<std::string> listSerialPorts();

// MAKCU / ESP32-S3 bridge firmware. Both speak the MAKCU text protocol for the mouse:
//   km.move(x,y)  km.wheel(n)  km.left(1|0)  km.right  km.middle  km.side1  km.side2
// The bridge firmware (firmware/esp32s3_bridge) also accepts kb.down(hid) / kb.up(hid).
class SerialBackend : public Backend {
public:
    SerialBackend(std::string port, int baud, bool keyboard, bool makcuHighSpeed = false);
    ~SerialBackend() override;
    bool open(std::string& error) override;
    void close() override;
    bool supportsKeyboard() const override { return keyboard_; }
    void move(int dx, int dy) override;
    void wheel(int clicks) override;
    void button(Button b, bool down) override;
    void key(const KeyInfo& k, bool down) override;

private:
    void send(const std::string& cmd);
    std::string port_;
    int baud_;
    bool keyboard_;
    bool highSpeed_;
    SerialPort serial_;
    std::mutex mu_;
    struct Drain;
    std::unique_ptr<Drain> drain_;
};

enum class Device { Makcu, Esp32, Software, DryRun };
struct DeviceInfo { Device id; const char* key; const char* label; };
extern const DeviceInfo kDevices[4];

// Software output (SendInput on Windows). Returns nullptr where unsupported.
std::unique_ptr<Backend> createSoftwareBackend();
std::unique_ptr<Backend> createBackend(Device d, const std::string& port, int baud);
