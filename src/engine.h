// Runs a Logitech G Hub / LGS Lua script and implements the G-series Lua API.
//
// Like G Hub, all script code runs on one worker thread: events are queued and
// OnEvent handles them one at a time, so handlers that loop with Sleep() and
// IsMouseButtonPressed() behave exactly as on a Logitech mouse.
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "backend.h"

struct lua_State;

// Read-only view of the real keyboard/mouse, provided by the platform layer.
class InputState {
public:
    virtual ~InputState() = default;
    virtual bool modifierPressed(const std::string& name) = 0;  // lshift, rshift, lctrl, rctrl, lalt, ralt
    virtual bool lockOn(const std::string& name) = 0;           // capslock, numlock, scrolllock
    virtual void cursorPos(int& x, int& y) = 0;
    virtual void screenRect(bool virtualDesktop, int& left, int& top, int& width, int& height) = 0;
};

class Engine {
public:
    using LogFn = std::function<void(const std::string&)>;

    // keyFallback is used for keys when `out` can't type (e.g. MAKCU); may be null.
    Engine(Backend& out, InputState& input, LogFn log, Backend* keyFallback = nullptr);
    ~Engine();

    bool start(const std::string& source, const std::string& chunkName, std::string& error);
    void stop();
    bool running() const { return running_; }

    // Called by the platform input hook for every real mouse button change.
    void onPhysicalButton(Button b, bool pressed);

    std::function<void()> onClearLog;

private:
    struct Event {
        std::string name;
        int arg = 0;
        std::string family;
        bool stop = false;
    };
    using Clock = std::chrono::steady_clock;

    void post(Event e);
    void run(std::string source, std::string chunkName, std::string* loadError, bool* ready);
    void callOnEvent(const Event& e);
    void releaseAll();
    bool shouldAbort();
    void sleepMs(double ms);
    Backend* keyOutput();

    // Lua API (C functions)
    static Engine* self(lua_State* L);
    static void hook(lua_State* L, struct lua_Debug* ar);
    static int l_log(lua_State* L);
    static int l_clearLog(lua_State* L);
    static int l_sleep(lua_State* L);
    static int l_runningTime(lua_State* L);
    static int l_key(lua_State* L);
    static int l_modifier(lua_State* L);
    static int l_lock(lua_State* L);
    static int l_mbutton(lua_State* L);
    static int l_mpressed(lua_State* L);
    static int l_moveRel(lua_State* L);
    static int l_wheel(lua_State* L);
    static int l_moveTo(lua_State* L);
    static int l_mousePos(lua_State* L);
    static int l_primary(lua_State* L);

    Backend& out_;
    InputState& input_;
    LogFn log_;
    Backend* keyFallback_;

    std::thread thread_;
    lua_State* L_ = nullptr;  // worker thread only
    std::atomic<bool> running_{false};
    std::atomic<bool> abort_{false};
    std::atomic<bool> primaryEvents_{false};
    std::atomic<bool> hasDeadline_{false};
    Clock::time_point deadline_;
    Clock::time_point start_;
    bool warnedKeyboard_ = false;

    std::mutex qmu_;
    std::condition_variable qcv_;
    std::deque<Event> queue_;

    std::mutex smu_;  // guards button state + echo lists
    std::array<bool, kButtonCount> physical_{};
    std::array<bool, kButtonCount> synthetic_{};
    std::array<std::deque<std::pair<bool, Clock::time_point>>, kButtonCount> echo_;
    std::set<const KeyInfo*> keysDown_;  // worker thread only
};
