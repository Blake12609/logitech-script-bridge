#include "backend.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

const char* buttonName(Button b) {
    static const char* names[] = {"left", "right", "middle", "side1", "side2"};
    return names[static_cast<int>(b)];
}

const DeviceInfo kDevices[kDeviceCount] = {
    {Device::Makcu, "makcu", "MAKCU", "Mouse only. Keys can be typed in software.", true, false},
    {Device::KmboxB, "kmbox-b", "KMBox B / B+ / B Pro", "Mouse only. Keys can be typed in software.", true, false},
    {Device::Esp32, "esp32", "ESP32-S3 (bridge firmware)", "Mouse + keyboard. Flash firmware/esp32s3_bridge.", true, true},
    {Device::Arduino, "arduino", "Arduino Leonardo / Pro Micro / Pi Pico (bridge firmware)",
     "Mouse + keyboard. Flash firmware/arduino_hid_bridge.", true, true},
    {Device::Software, "software", "Software (no hardware)", "Windows SendInput. Many games ignore it.", false, true},
    {Device::DryRun, "dry-run", "Dry run (log only)", "Nothing is sent; every action is written to the log.", false, true},
};

const DeviceInfo* findDevice(const std::string& key) {
    for (const auto& d : kDevices)
        if (key == d.key) return &d;
    return nullptr;
}

// ---------------------------------------------------------------- dry run

void DryRunBackend::emit(const std::string& s) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        sent_.push_back(s);
    }
    if (logFn_) logFn_(logCtx_, "[dry-run] " + s + "\n");
}

void DryRunBackend::move(int dx, int dy) { emit("move " + std::to_string(dx) + " " + std::to_string(dy)); }
void DryRunBackend::wheel(int clicks) { emit("wheel " + std::to_string(clicks)); }
void DryRunBackend::button(Button b, bool down) {
    emit(std::string("button ") + buttonName(b) + (down ? " 1" : " 0"));
}
void DryRunBackend::key(const KeyInfo& k, bool down) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "key 0x%02X %d", k.hid, down ? 1 : 0);
    emit(buf);
}

std::vector<std::string> DryRunBackend::sent() const {
    std::lock_guard<std::mutex> lock(mu_);
    return sent_;
}

// ---------------------------------------------------------------- serial device

// Devices echo every command back; keep reading so the OS buffer never fills up.
struct SerialBackend::Drain {
    std::atomic<bool> stop{false};
    std::thread thread;
};

SerialBackend::SerialBackend(std::string port, int baud, bool keyboard, bool makcuHighSpeed)
    : KmBackend(keyboard), port_(std::move(port)), baud_(baud), highSpeed_(makcuHighSpeed) {}

SerialBackend::~SerialBackend() { close(); }

bool SerialBackend::open(std::string& error) {
    if (port_.empty()) {
        error = "No serial port selected.";
        return false;
    }
    if (!serial_.open(port_, baud_, error)) return false;
    if (highSpeed_) {
        // Magic sequence that switches MAKCU's serial link to 4 Mbaud.
        static const unsigned char k4M[] = {0xDE, 0xAD, 0x05, 0x00, 0xA5, 0x00, 0x09, 0x3D, 0x00};
        serial_.write(std::string(reinterpret_cast<const char*>(k4M), sizeof(k4M)));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        serial_.setBaud(4000000);
    }
    drain_ = std::make_unique<Drain>();
    Drain* d = drain_.get();
    SerialPort* s = &serial_;
    d->thread = std::thread([d, s] {
        char buf[256];
        while (!d->stop && s->read(buf, sizeof(buf)) >= 0) {
        }
    });
    return true;
}

void SerialBackend::close() {
    if (drain_) {
        drain_->stop = true;
        if (drain_->thread.joinable()) drain_->thread.join();
        drain_.reset();
    }
    serial_.close();
}

void SerialBackend::sendLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(mu_);
    serial_.write(line + "\r\n");
}

// ---------------------------------------------------------------- km protocol

void KmBackend::move(int dx, int dy) {
    if (dx || dy) sendLine("km.move(" + std::to_string(dx) + "," + std::to_string(dy) + ")");
}

void KmBackend::wheel(int clicks) {
    if (clicks) sendLine("km.wheel(" + std::to_string(clicks) + ")");
}

void KmBackend::button(Button b, bool down) {
    sendLine(std::string("km.") + buttonName(b) + (down ? "(1)" : "(0)"));
}

void KmBackend::key(const KeyInfo& k, bool down) {
    sendLine(std::string(down ? "kb.down(" : "kb.up(") + std::to_string(k.hid) + ")");
}

std::unique_ptr<Backend> createBackend(Device d, const std::string& port, int baud) {
    switch (d) {
        case Device::Makcu:
        case Device::KmboxB: return std::make_unique<SerialBackend>(port, baud, false);
        case Device::Esp32:
        case Device::Arduino: return std::make_unique<SerialBackend>(port, baud, true);
        case Device::Software: return createSoftwareBackend();
        case Device::DryRun: return std::make_unique<DryRunBackend>();
    }
    return nullptr;
}
