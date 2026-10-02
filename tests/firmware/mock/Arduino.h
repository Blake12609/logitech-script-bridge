// Minimal stand-in for the Arduino core so the firmware sketches compile and
// run on a PC. Only what the sketches use, with the real signatures.
#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

// Every HID action the firmware takes, as text ("move 3 -4 0", "press 0x01", ...).
inline std::vector<std::string> g_hid;

inline void hidLog(const char* fmt, ...) {
    char buf[64];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    g_hid.push_back(buf);
}

// The parts of Arduino's Stream the sketches use.
class Stream {
public:
    virtual ~Stream() = default;
    virtual int available() = 0;
    virtual int read() = 0;
    virtual size_t println(const char* s) = 0;
};

class MockSerial : public Stream {
public:
    void begin(unsigned long) {}
    int available() override { return static_cast<int>(in_.size()); }
    int read() override {
        if (in_.empty()) return -1;
        unsigned char c = static_cast<unsigned char>(in_.front());
        in_.pop_front();
        return c;
    }
    size_t println(const char* s) override {
        out += s;
        out += "\r\n";
        return std::strlen(s) + 2;
    }
    explicit operator bool() const { return true; }

    // test side
    void feed(const std::string& s) { in_.insert(in_.end(), s.begin(), s.end()); }
    std::string out;

private:
    std::deque<char> in_;
};

inline MockSerial Serial;
inline MockSerial Serial0;  // ESP32 with "USB CDC On Boot": the UART behind the board's COM port
