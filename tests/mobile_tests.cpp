// Phone bridge: MAKCU button stream parsing and scripts driven through MobileSession.
#include <mutex>
#include <string>
#include <vector>

#include "mobile_session.h"
#include "test.h"

namespace {

struct Change {
    Button b;
    bool pressed;
    bool operator==(const Change& o) const { return b == o.b && pressed == o.pressed; }
};
using Changes = std::vector<Change>;

Changes feed(MakcuButtonStream& s, const std::string& bytes) {
    Changes out;
    s.feed(bytes.data(), bytes.size(), [&](Button b, bool p) { out.push_back({b, p}); });
    return out;
}

struct Phone {
    std::mutex mu;
    std::string sent, logText;
    MobileSession session;
    std::string error;

    Phone()
        : session(
              [this](const std::string& s) {
                  std::lock_guard<std::mutex> lock(mu);
                  sent += s;
              },
              [this](const std::string& s) {
                  std::lock_guard<std::mutex> lock(mu);
                  logText += s;
              }) {}
    bool start(const std::string& src, MobileDevice d = MobileDevice::Makcu, int w = 1920, int h = 1080) {
        MobileOptions o;
        o.device = d;
        o.screenWidth = w;
        o.screenHeight = h;
        return session.start(src, "phone.lua", o, error);
    }
    void data(const std::string& bytes) { session.onSerialData(bytes.data(), bytes.size()); }
    std::string out() {
        std::lock_guard<std::mutex> lock(mu);
        return sent;
    }
    std::string log() {
        std::lock_guard<std::mutex> lock(mu);
        return logText;
    }
    void clear() {
        std::lock_guard<std::mutex> lock(mu);
        sent.clear();
    }
};

size_t count(const std::string& hay, const std::string& needle) {
    size_t n = 0;
    for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + 1)) n++;
    return n;
}

}  // namespace

TEST(makcu_stream_raw_mask_bytes) {
    MakcuButtonStream s;
    CHECK((feed(s, std::string("\x08", 1)) == Changes{{Button::Side1, true}}));
    CHECK((feed(s, std::string("\x18", 1)) == Changes{{Button::Side2, true}}));
    CHECK((feed(s, std::string("\x00", 1)) == Changes{{Button::Side1, false}, {Button::Side2, false}}));
    CHECK(s.mask() == 0);
}

TEST(makcu_stream_prefixed_masks_and_text) {
    MakcuButtonStream s;
    CHECK(feed(s, "km.version()\r\nMAKCU v3.2\r\n>>> ").empty());
    CHECK((feed(s, std::string("km.\x01", 4)) == Changes{{Button::Left, true}}));
    // masks that look like CR / LF: left+middle+back = 0x0D, right+back = 0x0A
    CHECK((feed(s, std::string("km.\x0D", 4)) ==
           Changes{{Button::Middle, true}, {Button::Side1, true}}));
    CHECK((feed(s, std::string("km.\x0A", 4)) ==
           Changes{{Button::Left, false}, {Button::Right, true}, {Button::Middle, false}}));
    CHECK((feed(s, std::string("km.\x00", 4)) == Changes{{Button::Right, false}, {Button::Side1, false}}));
}

TEST(makcu_stream_cr_lf_masks_between_replies) {
    MakcuButtonStream s;
    CHECK(feed(s, "ok\r\n").empty());
    // no text pending: a lone 0x0A / 0x0D byte is a button mask
    CHECK((feed(s, std::string("\x0A", 1)) == Changes{{Button::Right, true}, {Button::Side1, true}}));
    CHECK((feed(s, std::string("\x0D", 1)) ==
           Changes{{Button::Left, true}, {Button::Right, false}, {Button::Middle, true}}));
    CHECK((feed(s, std::string("\x00", 1)).size() == 3));
}

TEST(phone_side_button_from_makcu_stream) {
    Phone p;
    CHECK(p.start(R"(
    function OnEvent(event, arg)
        if event == "MOUSE_BUTTON_PRESSED" and arg == 5 then
            repeat
                PressAndReleaseMouseButton(1)
                Sleep(20)
            until not IsMouseButtonPressed(5)
        end
    end)"));
    test::sleepMs(50);
    p.data(std::string("\x10", 1));  // forward button down
    test::sleepMs(150);
    CHECK((p.session.buttonMask() & 0x10) != 0);
    p.data(std::string("\x00", 1));
    test::sleepMs(100);
    const std::string out = p.out();
    CHECK(count(out, "km.left(1)\r\n") >= 3);
    CHECK(count(out, "km.left(1)\r\n") == count(out, "km.left(0)\r\n"));
    p.clear();
    test::sleepMs(100);
    CHECK(p.out().empty());  // stopped clicking after the release
    p.session.stop();
}

TEST(phone_touch_buttons_and_gkeys) {
    Phone p;
    CHECK(p.start(R"(
    function OnEvent(event, arg, family)
        OutputLogMessage("%s %d %s\n", event, arg, family)
        if event == "MOUSE_BUTTON_PRESSED" and arg == 4 then PressMouseButton(3) end
        if event == "MOUSE_BUTTON_RELEASED" and arg == 4 then ReleaseMouseButton(3) end
    end)", MobileDevice::KmboxB));
    p.session.onTouchButton(4, true);
    test::sleepMs(80);
    CHECK(p.session.buttonMask() == (0x08 | (0x02 << 8)));  // 4 held, script holds right (2)
    p.session.onTouchButton(4, false);
    p.session.onGKey(3, true);
    test::sleepMs(80);
    CHECK(p.out() == "km.right(1)\r\nkm.right(0)\r\n");
    CHECK(p.log().find("MOUSE_BUTTON_PRESSED 4 mouse") != std::string::npos);
    CHECK(p.log().find("G_PRESSED 3 kb") != std::string::npos);
    // a KMBox doesn't send button bytes; anything it echoes is ignored
    p.data(std::string("\x08", 1));
    test::sleepMs(30);
    CHECK((p.session.buttonMask() & 0xFF) == 0);
    p.session.stop();
}

TEST(phone_script_clicks_do_not_swallow_real_ones) {
    Phone p;
    CHECK(p.start(R"(
    function OnEvent(event, arg)
        if event == "PROFILE_ACTIVATED" then
            EnablePrimaryMouseButtonEvents(true)
            PressAndReleaseMouseButton(1)
        end
        if event == "MOUSE_BUTTON_PRESSED" then OutputLogMessage("real %d\n", arg) end
    end)"));
    test::sleepMs(50);
    p.data(std::string("\x01", 1));  // the user clicks right after the script did
    test::sleepMs(50);
    CHECK(p.log().find("real 1") != std::string::npos);
    p.session.stop();
}

TEST(phone_move_mouse_to_counts_from_the_corner) {
    Phone p;
    CHECK(p.start(R"(
    function OnEvent(event)
        if event == "PROFILE_ACTIVATED" then
            MoveMouseTo(32767, 32767)
            local x, y = GetMousePosition()
            OutputLogMessage("pos %d %d\n", x, y)
            MoveMouseRelative(10, 0)
            x, y = GetMousePosition()
            OutputLogMessage("pos %d %d\n", x, y)
        end
    end)", MobileDevice::Makcu, 1000, 500));
    test::sleepMs(100);
    const std::string out = p.out();
    CHECK(count(out, "km.move(-127,-127)\r\n") == 16);  // 2 x 1000 px pushes it into the corner
    CHECK(out.find("km.move(-127,-127)\r\nkm.move(499,249)\r\nkm.move(10,0)\r\n") != std::string::npos);
    CHECK(p.log().find("pos 32735 32702") != std::string::npos);  // 499/999, 249/499
    CHECK(p.log().find("pos 33391 32702") != std::string::npos);  // 509/999
    p.session.stop();
}

TEST(phone_keys_need_a_keyboard_device) {
    const char* script = R"(function OnEvent(e) if e == "PROFILE_ACTIVATED" then PressAndReleaseKey("a") end end)";
    Phone makcu;
    CHECK(makcu.start(script, MobileDevice::Makcu));
    test::sleepMs(50);
    CHECK(makcu.out().empty());
    CHECK(makcu.log().find("cannot send keys") != std::string::npos);
    makcu.session.stop();

    Phone esp;
    CHECK(esp.start(script, MobileDevice::Esp32));
    test::sleepMs(50);
    CHECK(esp.out() == "kb.down(4)\r\nkb.up(4)\r\n");
    esp.session.stop();
}

TEST(phone_demo_mode_logs_instead_of_sending) {
    Phone p;
    CHECK(p.start(R"(function OnEvent(e) if e == "PROFILE_ACTIVATED" then MoveMouseRelative(3, 4) end end)",
                  MobileDevice::Demo));
    test::sleepMs(50);
    CHECK(p.out().empty());
    CHECK(p.log().find("[demo] km.move(3,4)") != std::string::npos);
    p.session.stop();
}

TEST(phone_connect_commands) {
    CHECK(MobileSession::connectCommands(MobileDevice::Makcu) == "km.buttons(1)\r\n");
    CHECK(MobileSession::connectCommands(MobileDevice::Esp32).empty());
}

TEST(phone_load_error_is_reported) {
    Phone p;
    CHECK(!p.start("function OnEvent(", MobileDevice::Demo));
    CHECK(p.error.find("phone.lua:1:") != std::string::npos);
    CHECK(!p.session.running());
}

TEST(phone_on_screen_modifiers_and_locks) {
    Phone p;
    CHECK(p.start(R"(
    function OnEvent(event, arg)
        if event == "MOUSE_BUTTON_PRESSED" and arg == 4 then
            OutputLogMessage("ctrl=%s rctrl=%s shift=%s alt=%s caps=%s num=%s scroll=%s\n",
                tostring(IsModifierPressed("ctrl")), tostring(IsModifierPressed("rctrl")),
                tostring(IsModifierPressed("lshift")), tostring(IsModifierPressed("alt")),
                tostring(IsKeyLockOn("capslock")), tostring(IsKeyLockOn("numlock")),
                tostring(IsKeyLockOn("scrolllock")))
        end
    end)", MobileDevice::Demo));
    p.session.onTouchButton(4, true);
    p.session.onTouchButton(4, false);
    test::sleepMs(50);
    p.session.setKeys(MobileSession::Ctrl | MobileSession::CapsLock);
    p.session.onTouchButton(4, true);
    p.session.onTouchButton(4, false);
    test::sleepMs(50);
    p.session.setKeys(MobileSession::Shift | MobileSession::Alt | MobileSession::NumLock | MobileSession::ScrollLock);
    p.session.onTouchButton(4, true);
    p.session.onTouchButton(4, false);
    test::sleepMs(50);
    const std::string log = p.log();
    CHECK(log.find("ctrl=false rctrl=false shift=false alt=false caps=false num=false scroll=false") != std::string::npos);
    CHECK(log.find("ctrl=true rctrl=true shift=false alt=false caps=true num=false scroll=false") != std::string::npos);
    CHECK(log.find("ctrl=false rctrl=false shift=true alt=true caps=false num=true scroll=true") != std::string::npos);
    p.session.stop();
}
