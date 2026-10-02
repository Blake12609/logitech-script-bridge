#include "mobile_session.h"

#include <algorithm>

// ------------------------------------------------------------------ MAKCU button stream

void MakcuButtonStream::reset() {
    line_.clear();
    lastCR_ = false;
    mask_ = 0;
}

void MakcuButtonStream::apply(int mask, const ChangeFn& onChange) {
    mask &= 0x1F;
    const int changed = mask ^ mask_;
    mask_ = mask;
    for (int bit = 0; bit < kButtonCount; bit++)
        if (changed & (1 << bit)) onChange(static_cast<Button>(bit), (mask >> bit) & 1);
}

void MakcuButtonStream::feed(const char* data, size_t size, const ChangeFn& onChange) {
    for (size_t i = 0; i < size; i++) {
        const unsigned char b = static_cast<unsigned char>(data[i]);
        const bool afterCR = lastCR_;
        lastCR_ = false;
        const bool prefixed = line_.size() >= 3 && line_.compare(line_.size() - 3, 3, "km.") == 0;
        if (b < 0x20 && prefixed) {
            // "km." + mask byte; a mask can look like CR or LF, so this check comes first
            line_.resize(line_.size() - 3);
            apply(b, onChange);
        } else if (b == '\r' && !line_.empty()) {
            line_.clear();  // end of a text reply
            lastCR_ = true;
        } else if (b == '\n' && (afterCR || !line_.empty())) {
            line_.clear();
        } else if (b < 0x20) {
            apply(b, onChange);
        } else if (line_.size() < 256) {
            line_ += static_cast<char>(b);
        } else {
            line_.clear();
        }
    }
}

// ------------------------------------------------------------------ output + pointer tracking

// Sends km.* lines to the device (or to the log in demo mode) and keeps track of
// where the PC's pointer should be, since the phone can't see it.
class MobileSession::Output : public KmBackend {
public:
    explicit Output(MobileSession& s) : KmBackend(false), s_(s) {}

    void configure(const MobileOptions& o) {
        device_ = o.device;
        w_ = std::max(o.screenWidth, 1);
        h_ = std::max(o.screenHeight, 1);
        x_ = w_ / 2;
        y_ = h_ / 2;
    }
    bool supportsKeyboard() const override {
        return device_ == MobileDevice::Esp32 || device_ == MobileDevice::Demo;
    }
    void move(int dx, int dy) override {
        x_ = std::clamp(x_ + dx, 0, w_ - 1);
        y_ = std::clamp(y_ + dy, 0, h_ - 1);
        KmBackend::move(dx, dy);
    }
    void pos(int& x, int& y) const { x = x_; y = y_; }
    void size(int& w, int& h) const { w = w_; h = h_; }

    // Push the pointer into the top-left corner, so MoveMouseTo can count from there.
    void home() {
        if (device_ == MobileDevice::Demo) {
            s_.log_("[demo] pointer to the top-left corner\n");
        } else {
            for (int left = std::max(w_, h_) * 2; left > 0; left -= 127) sendLine("km.move(-127,-127)");
        }
        x_ = y_ = 0;
    }

protected:
    void sendLine(const std::string& line) override {
        if (device_ == MobileDevice::Demo)
            s_.log_("[demo] " + line + "\n");
        else
            s_.write_(line + "\r\n");
    }

private:
    MobileSession& s_;
    MobileDevice device_ = MobileDevice::Makcu;
    int w_ = 1920, h_ = 1080, x_ = 960, y_ = 540;  // engine worker thread only
};

class MobileSession::Input : public InputState {
public:
    explicit Input(Output& out) : out_(out) {}
    // The phone can't see the PC's keyboard.
    bool modifierPressed(const std::string&) override { return false; }
    bool lockOn(const std::string&) override { return false; }
    void cursorPos(int& x, int& y) override { out_.pos(x, y); }
    void screenRect(bool, int& left, int& top, int& width, int& height) override {
        left = top = 0;
        out_.size(width, height);
    }
    void beforeMoveTo() override { out_.home(); }

private:
    Output& out_;
};

// ------------------------------------------------------------------ session

MobileSession::MobileSession(WriteFn write, LogFn log) : write_(std::move(write)), log_(std::move(log)) {
    out_ = std::make_unique<Output>(*this);
    input_ = std::make_unique<Input>(*out_);
    engine_ = std::make_unique<Engine>(*out_, *input_, [this](const std::string& s) { log_(s); });
    engine_->setExpectEchoes(false);  // the button sources only see the real mouse
    engine_->onClearLog = [this] {
        if (onClearLog) onClearLog();
    };
}

MobileSession::~MobileSession() { engine_.reset(); }

void MobileSession::setDevice(MobileDevice d) {
    if (d == device_) return;
    device_ = d;
    std::lock_guard<std::mutex> lock(streamMu_);
    stream_.reset();
}

std::string MobileSession::connectCommands(MobileDevice d) {
    if (d == MobileDevice::Makcu) return "km.echo(0)\r\nkm.buttons(1)\r\n";
    return "";
}

bool MobileSession::start(const std::string& source, const std::string& chunkName, const MobileOptions& opt,
                          std::string& error) {
    engine_->stop();
    setDevice(opt.device);
    out_->configure(opt);
    engine_->setJitter(opt.jitterMin, opt.jitterMax);
    return engine_->start(source, chunkName, error);
}

void MobileSession::stop() { engine_->stop(); }

bool MobileSession::running() const { return engine_->running(); }

void MobileSession::onSerialData(const char* data, size_t size) {
    if (device_ != MobileDevice::Makcu) return;  // other devices only echo text
    std::lock_guard<std::mutex> lock(streamMu_);
    stream_.feed(data, size, [this](Button b, bool pressed) { engine_->onPhysicalButton(b, pressed); });
}

void MobileSession::onTouchButton(int n, bool pressed) {
    if (n < 1 || n > kButtonCount) return;
    engine_->onPhysicalButton(static_cast<Button>(n - 1), pressed);  // Button order = OnEvent numbering
}

void MobileSession::onGKey(int n, bool pressed) { engine_->onGKey(n, pressed); }

int MobileSession::buttonMask() {
    int mask = 0;
    for (int n = 1; n <= kButtonCount; n++) {
        bool physical, script;
        engine_->buttonState(n, physical, script);
        if (physical) mask |= 1 << (n - 1);
        if (script) mask |= 1 << (n - 1 + 8);
    }
    return mask;
}
