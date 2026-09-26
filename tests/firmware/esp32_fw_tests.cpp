// firmware/esp32s3_bridge compiled against the mock ESP32 Arduino core.
#include "Arduino.h"
#include "../../firmware/esp32s3_bridge/esp32s3_bridge.ino"
#include "fw_link.h"

namespace {
struct Setup {
    Setup() { setup(); }
} runSetupOnce;
}  // namespace

TEST(esp32_lua_to_hid_end_to_end) {
    Hid hid = runScriptThroughFirmware(R"(
    function OnEvent(event)
        if event == "PROFILE_ACTIVATED" then
            MoveMouseRelative(300, -5)
            PressKey("lshift", "a")
            ReleaseKey("a", "lshift")
            PressAndReleaseMouseButton(3)
            PressAndReleaseMouseButton(4)
            PressAndReleaseMouseButton(5)
            MoveMouseWheel(2)
        end
    end)");
    CHECK((hid == Hid{"move 127 -5 0", "move 127 0 0", "move 46 0 0",       // split into HID-sized steps
                      "keydown 0xE1", "keydown 0x04", "keyup 0x04", "keyup 0xE1",
                      "press 0x02", "release 0x02",                          // 3 = right
                      "press 0x08", "release 0x08",                          // 4 = back
                      "press 0x10", "release 0x10",                          // 5 = forward
                      "move 0 0 2"}));
}

TEST(esp32_releases_held_input_when_script_stops) {
    Hid hid = runScriptThroughFirmware(R"(
    function OnEvent(event)
        if event == "PROFILE_ACTIVATED" then
            PressMouseButton(1)
            PressKey("w")
            while true do end
        end
    end)");
    CHECK((hid == Hid{"press 0x01", "keydown 0x1A", "release 0x01", "keyup 0x1A"}));
}

TEST(esp32_parser_edge_cases) {
    CHECK((feed("km.move(-300,0)\n") == Hid{"move -127 0 0", "move -127 0 0", "move -46 0 0"}));
    CHECK((feed("kb.down(0x2C)\r\nkb.up(44)\r\n") == Hid{"keydown 0x2C", "keyup 0x2C"}));
    CHECK((feed("km.wheel(-500)\n") == Hid{"move 0 0 -127"}));
    CHECK(feed("garbage\nkm.left\nkm.nope(1)\nkm.move(1)\n\n\r\n").empty());
    // a line longer than the buffer is cut, not overflowed; the next line still works
    CHECK((feed(std::string(200, 'x') + "\nkm.middle(1)\n") == Hid{"press 0x04"}));
    // a command split across two reads
    CHECK(feed("km.ri").empty());
    CHECK((feed("ght(1)\n") == Hid{"press 0x02"}));
    Serial.out.clear();
    feed("km.version()\n");
    CHECK(Serial.out.find("esp32s3") != std::string::npos);
}
