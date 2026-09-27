// Engine tests: run Lua scripts against a dry-run device and fake input.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "backend.h"
#include "engine.h"
#include "test.h"

namespace {

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
    static void settle(int ms = 100) { test::sleepMs(ms); }
};

using Sent = std::vector<std::string>;

TEST(profile_events_and_log) {
    Harness h(R"(function OnEvent(e, a, f) OutputLogMessage("%s %d\n", e, a) end)");
    CHECK(h.started);
    Harness::settle();
    CHECK(h.logHas("PROFILE_ACTIVATED 0\n"));
    h.engine.stop();
    CHECK(h.logHas("PROFILE_DEACTIVATED 0\n"));
}

TEST(side_button_presses_key_combo) {
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

TEST(scancodes_are_accepted) {
    Harness h(R"(function OnEvent(e) if e == "PROFILE_ACTIVATED" then PressKey(0x1E) ReleaseKey(0x1E) end end)");
    Harness::settle();
    CHECK((h.backend.sent() == Sent{"key 0x04 1", "key 0x04 0"}));
}

TEST(primary_button_needs_enable) {
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

TEST(button_numbering_matches_logitech) {
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

TEST(hold_loop_runs_until_release) {
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

TEST(injected_clicks_are_not_reported_back) {
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

TEST(stop_interrupts_busy_loop_and_releases) {
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

TEST(stop_interrupts_sleep) {
    Harness h(R"(function OnEvent(e) if e == "PROFILE_ACTIVATED" then Sleep(60000) end end)");
    Harness::settle();
    auto t0 = std::chrono::steady_clock::now();
    h.engine.stop();
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(1));
}

TEST(keyboard_ignored_when_device_cannot_type) {
    Harness h(R"(function OnEvent(e) if e == "PROFILE_ACTIVATED" then PressAndReleaseKey("a") end end)", false);
    Harness::settle();
    CHECK(h.backend.sent().empty());
    CHECK(h.logHas("cannot send keys"));
}

TEST(modifiers_and_locks) {
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

TEST(sandbox) {
    Harness h(R"(
    OutputLogMessage("io=%s execute=%s debug=%s require=%s\n",
        tostring(io), tostring(os.execute), tostring(debug), tostring(require))
    OutputLogMessage("bytecode=%s\n", tostring(load(string.dump(function() end)) ~= nil))
    function OnEvent() end)");
    Harness::settle();
    CHECK(h.logHas("io=nil execute=nil debug=nil require=nil"));
    CHECK(h.logHas("bytecode=false"));
}

TEST(load_errors) {
    Harness a("function OnEvent(");
    CHECK(!a.started);
    CHECK(a.error.find("test.lua") != std::string::npos);
    Harness b("x = 1");
    CHECK(!b.started);
    CHECK(b.error.find("OnEvent") != std::string::npos);
}

TEST(runtime_error_is_logged_and_script_keeps_running) {
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

TEST(mouse_position_and_compat) {
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

TEST(restart) {
    Harness h(R"(function OnEvent(e) OutputLogMessage("%s\n", e) end)");
    Harness::settle();
    h.engine.stop();
    std::string err;
    CHECK(h.engine.start(R"(function OnEvent(e) OutputLogMessage("second %s\n", e) end)", "b.lua", err));
    Harness::settle();
    CHECK(h.logHas("second PROFILE_ACTIVATED"));
}

TEST(extra_buttons_6_and_up) {
    Harness h(R"(
    function OnEvent(event, arg, family)
        if event == "MOUSE_BUTTON_PRESSED" and arg == 6 then
            OutputLogMessage("pressed 6 %s held=%s\n", family, tostring(IsMouseButtonPressed(6)))
        elseif event == "MOUSE_BUTTON_RELEASED" and arg == 6 then
            OutputLogMessage("released 6 held=%s\n", tostring(IsMouseButtonPressed(6)))
        end
    end)");
    h.engine.onExtraButton(6, true);
    h.engine.onExtraButton(6, true);  // key auto-repeat must not fire twice
    Harness::settle();
    h.engine.onExtraButton(6, false);
    Harness::settle();
    const std::string log = h.log();
    CHECK(log.find("pressed 6 mouse held=true") != std::string::npos);
    CHECK(log.find("pressed 6", log.find("pressed 6") + 1) == std::string::npos);
    CHECK(log.find("released 6 held=false") != std::string::npos);
}

TEST(g_keys) {
    Harness h(R"(
    function OnEvent(event, arg, family)
        if event == "G_PRESSED" or event == "G_RELEASED" then
            OutputLogMessage("%s %d %s\n", event, arg, family)
        end
    end)");
    h.engine.onGKey(3, true);
    h.engine.onGKey(3, false);
    Harness::settle();
    CHECK(h.logHas("G_PRESSED 3 kb\nG_RELEASED 3 kb"));
}

// Runs 500 one-pixel moves to the right and returns how far off the exact path each step landed.
std::vector<double> jitterDistances(double minPx, double maxPx, bool* varied) {
    DryRunBackend backend;
    NullInput input;
    Engine engine(backend, input, [](const std::string&) {});
    engine.setJitter(minPx, maxPx, 1234);
    std::string err;
    engine.start(R"(
    function OnEvent(e)
        if e == "PROFILE_ACTIVATED" then
            for i = 1, 500 do MoveMouseRelative(1, 0) end
        end
    end)", "jitter.lua", err);
    Harness::settle(200);
    engine.stop();
    std::vector<double> dist;
    int x = 0, y = 0;
    *varied = false;
    for (const auto& line : backend.sent()) {
        int dx = 0, dy = 0;
        if (std::sscanf(line.c_str(), "move %d %d", &dx, &dy) != 2) continue;
        x += dx;
        y += dy;
        if (dx != 1 || dy != 0) *varied = true;
        const double ox = x - static_cast<double>(dist.size() + 1), oy = y;  // offset from the exact spot
        dist.push_back(std::sqrt(ox * ox + oy * oy));
    }
    return dist;
}

TEST(jitter_stays_within_min_max) {
    bool varied = false;
    const auto dist = jitterDistances(1.0, 4.0, &varied);
    CHECK(dist.size() == 500);
    CHECK(varied);
    double lo = 1e9, hi = 0, sum = 0;
    for (double d : dist) {
        lo = std::min(lo, d);
        hi = std::max(hi, d);
        sum += d;
    }
    // whole-pixel rounding can shift the distance by up to ~0.71 px
    CHECK(lo >= 1.0 - 0.71);
    CHECK(hi <= 4.0 + 0.71);
    CHECK(sum / dist.size() > 1.8 && sum / dist.size() < 3.2);  // spread across the range, no drift
}

TEST(jitter_decimals_and_swapped_range) {
    bool varied = false;
    auto dist = jitterDistances(2.5, 1.5, &varied);  // min/max given the wrong way round
    CHECK(varied);
    for (double d : dist) CHECK(d >= 1.5 - 0.71 && d <= 2.5 + 0.71);
    dist = jitterDistances(0.2, 0.6, &varied);  // sub-pixel: mostly 0 or 1 px off
    for (double d : dist) CHECK(d <= 0.6 + 0.71);
}

TEST(jitter_off_is_exact) {
    Harness h(R"(function OnEvent(e) if e == "PROFILE_ACTIVATED" then MoveMouseRelative(5, -4) MoveMouseRelative(-2, 7) end end)");
    Harness::settle();
    CHECK((h.backend.sent() == Sent{"move 5 -4", "move -2 7"}));
}

TEST(button_state_for_indicator) {
    Harness h(R"(
    function OnEvent(e, a)
        if e == "MOUSE_BUTTON_PRESSED" and a == 4 then PressMouseButton(3) end
    end)");
    h.engine.onPhysicalButton(Button::Side1, true);
    Harness::settle();
    bool phys = false, script = false;
    h.engine.buttonState(4, phys, script);
    CHECK(phys && !script);
    h.engine.buttonState(2, phys, script);  // PressMouseButton(3) is the right button = OnEvent 2
    CHECK(!phys && script);
    h.engine.buttonState(9, phys, script);
    CHECK(!phys && !script);
}

}  // namespace
