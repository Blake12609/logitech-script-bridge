// Engine tests: no hardware needed, run with `ctest` or ./engine_tests
#include <chrono>
#include <cstdio>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "backend.h"
#include "engine.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

class NullInput : public InputState {
public:
    std::set<std::string> mods, locks;
    int x = 0, y = 0;
    bool modifierPressed(const std::string& n) override { return mods.count(n) > 0; }
    bool lockOn(const std::string& n) override { return locks.count(n) > 0; }
    void cursorPos(int& ox, int& oy) override { ox = x; oy = y; }
    void screenRect(bool, int& l, int& t, int& w, int& h) override { l = 0; t = 0; w = 1920; h = 1080; }
};

struct Harness {
    DryRunBackend backend;
    NullInput input;
    std::mutex mu;
    std::string logText;
    Engine engine;
    std::string error;
    bool started;

    explicit Harness(const std::string& src, bool keyboard = true,
                     const std::function<void(NullInput&)>& setup = nullptr)
        : backend(keyboard),
          engine(backend, input, [this](const std::string& s) {
              std::lock_guard<std::mutex> lock(mu);
              logText += s;
          }) {
        if (setup) setup(input);
        started = engine.start(src, "test.lua", error);
    }
    std::string log() {
        std::lock_guard<std::mutex> lock(mu);
        return logText;
    }
    bool logHas(const std::string& s) { return log().find(s) != std::string::npos; }
    static void settle(int ms = 100) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
};

using Sent = std::vector<std::string>;

void test_profile_events_and_log() {
    Harness h(R"(function OnEvent(e, a, f) OutputLogMessage("%s %d\n", e, a) end)");
    CHECK(h.started);
    Harness::settle();
    CHECK(h.logHas("PROFILE_ACTIVATED 0\n"));
    h.engine.stop();
    CHECK(h.logHas("PROFILE_DEACTIVATED 0\n"));
}

void test_side_button_presses_key_combo() {
    Harness h(R"(
    function OnEvent(event, arg)
        if event == "MOUSE_BUTTON_PRESSED" and arg == 4 then
            PressAndReleaseKey("lctrl", "c")
        end
    end)");
    h.engine.onPhysicalButton(Button::Side1, true);
    Harness::settle();
    CHECK((h.backend.sent() == Sent{"key 0xE0 1", "key 0x06 1", "key 0xE0 0", "key 0x06 0"}));
}

void test_scancodes_are_accepted() {
    Harness h(R"(function OnEvent(e) if e == "PROFILE_ACTIVATED" then PressKey(0x1E) ReleaseKey(0x1E) end end)");
    Harness::settle();
    CHECK((h.backend.sent() == Sent{"key 0x04 1", "key 0x04 0"}));
}

void test_primary_button_needs_enable() {
    Harness h(R"(
    function OnEvent(event, arg)
        if event == "MOUSE_BUTTON_PRESSED" then OutputLogMessage("press %d\n", arg) end
    end)");
    Harness::settle();
    h.engine.onPhysicalButton(Button::Left, true);
    h.engine.onPhysicalButton(Button::Right, true);
    Harness::settle();
    CHECK(!h.logHas("press 1"));
    CHECK(h.logHas("press 2"));
}

void test_button_numbering_matches_logitech() {
    // OnEvent: 2 = right, 3 = middle.  PressMouseButton/IsMouseButtonPressed: 2 = middle, 3 = right.
    Harness h(R"(
    function OnEvent(event, arg)
        if event == "MOUSE_BUTTON_PRESSED" and arg == 2 then
            OutputLogMessage("right=%s middle=%s\n", tostring(IsMouseButtonPressed(3)), tostring(IsMouseButtonPressed(2)))
            PressMouseButton(2)
            ReleaseMouseButton(2)
        end
    end)");
    h.engine.onPhysicalButton(Button::Right, true);
    Harness::settle();
    CHECK(h.logHas("right=true middle=false"));
    CHECK((h.backend.sent() == Sent{"button middle 1", "button middle 0"}));
}

void test_hold_loop_runs_until_release() {
    Harness h(R"(
    function OnEvent(event, arg)
        if event == "MOUSE_BUTTON_PRESSED" and arg == 5 then
            repeat
                MoveMouseRelative(0, 1)
                Sleep(10)
            until not IsMouseButtonPressed(5)
            OutputLogMessage("done\n")
        end
    end)");
    h.engine.onPhysicalButton(Button::Side2, true);
    Harness::settle(150);
    h.engine.onPhysicalButton(Button::Side2, false);
    Harness::settle(50);
    CHECK(h.logHas("done"));
    size_t moves = h.backend.sent().size();
    CHECK(moves >= 5 && moves <= 30);
}

void test_injected_clicks_are_not_reported_back() {
    Harness h(R"(
    function OnEvent(event, arg)
        if event == "PROFILE_ACTIVATED" then EnablePrimaryMouseButtonEvents(true) end
        if event == "MOUSE_BUTTON_PRESSED" then OutputLogMessage("press %d\n", arg) end
        if event == "MOUSE_BUTTON_PRESSED" and arg == 4 then PressAndReleaseMouseButton(1) end
    end)");
    Harness::settle();
    h.engine.onPhysicalButton(Button::Side1, true);
    Harness::settle();
    // the hardware click reaches the PC like a real one; it must be swallowed
    h.engine.onPhysicalButton(Button::Left, true);
    h.engine.onPhysicalButton(Button::Left, false);
    Harness::settle();
    CHECK(h.logHas("press 4"));
    CHECK(!h.logHas("press 1"));
}

void test_stop_interrupts_busy_loop_and_releases() {
    Harness h(R"(
    function OnEvent(event, arg)
        if event == "PROFILE_ACTIVATED" then
            PressMouseButton(1)
            PressKey("w")
            while true do end
        end
    end)");
    Harness::settle();
    auto t0 = std::chrono::steady_clock::now();
    h.engine.stop();
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2));
    Sent s = h.backend.sent();
    CHECK(s.size() == 4);
    CHECK(s.size() == 4 && s[2] == "button left 0" && s[3] == "key 0x1A 0");
}

void test_stop_interrupts_sleep() {
    Harness h(R"(function OnEvent(e) if e == "PROFILE_ACTIVATED" then Sleep(60000) end end)");
    Harness::settle();
    auto t0 = std::chrono::steady_clock::now();
    h.engine.stop();
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(1));
}

void test_keyboard_ignored_when_device_cannot_type() {
    Harness h(R"(function OnEvent(e) if e == "PROFILE_ACTIVATED" then PressAndReleaseKey("a") end end)", false);
    Harness::settle();
    CHECK(h.backend.sent().empty());
    CHECK(h.logHas("cannot send keys"));
}

void test_modifiers_and_locks() {
    Harness h(R"(
    function OnEvent(e, a)
        if a == 4 then
            OutputLogMessage("%s %s %s\n", tostring(IsModifierPressed("shift")),
                tostring(IsModifierPressed("lctrl")), tostring(IsKeyLockOn("capslock")))
        end
    end)", true, [](NullInput& in) {
        in.mods.insert("rshift");
        in.locks.insert("capslock");
    });
    h.engine.onPhysicalButton(Button::Side1, true);
    Harness::settle();
    CHECK(h.logHas("true false true"));
}

void test_sandbox() {
    Harness h(R"(
    OutputLogMessage("io=%s execute=%s debug=%s require=%s\n",
        tostring(io), tostring(os.execute), tostring(debug), tostring(require))
    OutputLogMessage("bytecode=%s\n", tostring(load(string.dump(function() end)) ~= nil))
    function OnEvent() end)");
    Harness::settle();
    CHECK(h.logHas("io=nil execute=nil debug=nil require=nil"));
    CHECK(h.logHas("bytecode=false"));
}

void test_load_errors() {
    Harness a("function OnEvent(");
    CHECK(!a.started);
    CHECK(a.error.find("test.lua") != std::string::npos);
    Harness b("x = 1");
    CHECK(!b.started);
    CHECK(b.error.find("OnEvent") != std::string::npos);
}

void test_runtime_error_is_logged_and_script_keeps_running() {
    Harness h(R"(
    function OnEvent(e, a)
        if a == 4 then error("oops") end
        if a == 5 then OutputLogMessage("still alive\n") end
    end)");
    h.engine.onPhysicalButton(Button::Side1, true);
    h.engine.onPhysicalButton(Button::Side2, true);
    Harness::settle();
    CHECK(h.logHas("test.lua:3: oops"));
    CHECK(h.logHas("still alive"));
}

void test_mouse_position_and_compat() {
    Harness h(R"(
    function OnEvent(e)
        if e == "PROFILE_ACTIVATED" then
            local x, y = GetMousePosition()
            local a, b = unpack({1, 2})
            OutputLogMessage("%d %d %d %d %s\n", x, y, a, b, tostring(GetMKeyState()))
        end
    end)", true, [](NullInput& in) { in.x = 1919; in.y = 0; });
    Harness::settle();
    CHECK(h.logHas("65535 0 1 2 1"));
}

void test_restart() {
    Harness h(R"(function OnEvent(e) OutputLogMessage("%s\n", e) end)");
    Harness::settle();
    h.engine.stop();
    std::string err;
    CHECK(h.engine.start(R"(function OnEvent(e) OutputLogMessage("second %s\n", e) end)", "b.lua", err));
    Harness::settle();
    CHECK(h.logHas("second PROFILE_ACTIVATED"));
}

}  // namespace

int main() {
    const struct { const char* name; std::function<void()> fn; } tests[] = {
        {"profile_events_and_log", test_profile_events_and_log},
        {"side_button_presses_key_combo", test_side_button_presses_key_combo},
        {"scancodes_are_accepted", test_scancodes_are_accepted},
        {"primary_button_needs_enable", test_primary_button_needs_enable},
        {"button_numbering_matches_logitech", test_button_numbering_matches_logitech},
        {"hold_loop_runs_until_release", test_hold_loop_runs_until_release},
        {"injected_clicks_are_not_reported_back", test_injected_clicks_are_not_reported_back},
        {"stop_interrupts_busy_loop_and_releases", test_stop_interrupts_busy_loop_and_releases},
        {"stop_interrupts_sleep", test_stop_interrupts_sleep},
        {"keyboard_ignored_when_device_cannot_type", test_keyboard_ignored_when_device_cannot_type},
        {"modifiers_and_locks", test_modifiers_and_locks},
        {"sandbox", test_sandbox},
        {"load_errors", test_load_errors},
        {"runtime_error_is_logged_and_script_keeps_running", test_runtime_error_is_logged_and_script_keeps_running},
        {"mouse_position_and_compat", test_mouse_position_and_compat},
        {"restart", test_restart},
    };
    for (const auto& t : tests) {
        int before = g_failures;
        t.fn();
        std::printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", t.name);
    }
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
