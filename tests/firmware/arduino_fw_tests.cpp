// firmware/arduino_hid_bridge compiled against mock Arduino Mouse/Keyboard libraries.
#include "Arduino.h"
#include "../../firmware/arduino_hid_bridge/arduino_hid_bridge.ino"
#include "fw_link.h"

namespace {
struct Setup {
    Setup() { setup(); }
} runSetupOnce;
}  // namespace

TEST(arduino_lua_to_hid_end_to_end) {
    Hid hid = runScriptThroughFirmware(R"(
    function OnEvent(event)
        if event == "PROFILE_ACTIVATED" then
            MoveMouseRelative(-130, 20)
            PressAndReleaseKey("lctrl", "c")
            PressAndReleaseKey("f24")
            PressAndReleaseMouseButton(1)
            PressAndReleaseMouseButton(2)
            PressAndReleaseMouseButton(5)
            MoveMouseWheel(-1)
        end
    end)");
    // Keyboard library codes: modifiers 0x80.., other keys HID usage + 136
    CHECK((hid == Hid{"move -127 20 0", "move -3 0 0",
                      "kpress 128", "kpress 142", "krelease 128", "krelease 142",
                      "kpress 251", "krelease 251",
                      "press 1", "release 1",
                      "press 4", "release 4",      // 2 = middle
                      "press 16", "release 16",    // 5 = forward
                      "move 0 0 -1"}));
}

TEST(arduino_parser_edge_cases) {
    CHECK((feed("kb.down(0xE5)\nkb.up(229)\n") == Hid{"kpress 133", "krelease 133"}));  // right shift
    CHECK(feed("kb.down(0)\nkb.down(300)\n").empty());  // out of range keys are ignored
    CHECK((feed("km.side1(1)\nkm.side1(0)\n") == Hid{"press 8", "release 8"}));
    CHECK(feed("km.move(0,0)\nnonsense\n").empty());
    Serial.out.clear();
    feed("km.version()\n");
    CHECK(Serial.out.find("arduino") != std::string::npos);
}
