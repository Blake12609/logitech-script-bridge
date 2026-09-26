// Logitech Script Bridge - Arduino firmware
// For boards with native USB: Arduino Leonardo, Micro, Pro Micro (ATmega32U4)
// and Raspberry Pi Pico / RP2040 boards (Arduino-Pico core by Earle Philhower).
//
// Turns the board into a USB mouse + keyboard driven by text commands over
// its USB serial port, using the same commands as the MAKCU plus keyboard:
//
//   km.move(x,y)    relative move          km.wheel(n)    scroll
//   km.left(1|0)    km.right(1|0)          km.middle(1|0)
//   km.side1(1|0)   km.side2(1|0)          (back / forward buttons, see below)
//   kb.down(hid)    kb.up(hid)             USB HID usage ID, e.g. 4 = 'a', 225 = left shift
//   km.version()
//
// Arduino IDE: pick your board, upload, done. On a Pi Pico choose
// Tools > USB Stack: "Pico SDK" (the default), which gives serial + HID.
//
// Back/forward buttons: the stock Mouse library of AVR boards (Leonardo,
// Pro Micro) only reports three buttons, so km.side1/side2 do nothing there.
// RP2040 boards report five buttons and support them.

#include <Mouse.h>
#include <Keyboard.h>

const uint8_t BUTTON_BACK = 0x08;
const uint8_t BUTTON_FORWARD = 0x10;

static char line[64];
static size_t lineLen = 0;

static void moveBy(long dx, long dy) {
  // HID reports carry at most +-127 per axis, so split large moves.
  while (dx != 0 || dy != 0) {
    int sx = dx > 127 ? 127 : (dx < -127 ? -127 : dx);
    int sy = dy > 127 ? 127 : (dy < -127 ? -127 : dy);
    Mouse.move(sx, sy, 0);
    dx -= sx;
    dy -= sy;
  }
}

static void setButton(uint8_t mask, long down) {
  if (down) Mouse.press(mask);
  else Mouse.release(mask);
}

// The Keyboard library takes 0x80-0x87 for modifiers and (HID usage + 136) for raw keys.
static uint8_t keyCode(long hid) {
  if (hid >= 0xE0 && hid <= 0xE7) return 0x80 + (hid - 0xE0);
  if (hid > 0 && hid < 0x78) return hid + 136;
  return 0;
}

// "km.move(3,-4)" -> name "km.move", args {3, -4}
static int parse(const char *s, char *name, size_t nameSize, long *args, int maxArgs) {
  const char *paren = strchr(s, '(');
  if (!paren) return -1;
  size_t n = paren - s;
  if (n >= nameSize) return -1;
  memcpy(name, s, n);
  name[n] = 0;
  int count = 0;
  const char *p = paren + 1;
  while (*p && *p != ')' && count < maxArgs) {
    char *end;
    long v = strtol(p, &end, 0);
    if (end == p) break;
    args[count++] = v;
    p = end;
    while (*p == ' ' || *p == ',') p++;
  }
  return count;
}

static void handle(const char *cmd) {
  char name[16];
  long a[2] = {0, 0};
  int n = parse(cmd, name, sizeof(name), a, 2);
  if (n < 0) return;

  if (!strcmp(name, "km.move") && n == 2) moveBy(a[0], a[1]);
  else if (!strcmp(name, "km.wheel") && n == 1) Mouse.move(0, 0, constrain(a[0], -127, 127));
  else if (!strcmp(name, "km.left") && n == 1) setButton(MOUSE_LEFT, a[0]);
  else if (!strcmp(name, "km.right") && n == 1) setButton(MOUSE_RIGHT, a[0]);
  else if (!strcmp(name, "km.middle") && n == 1) setButton(MOUSE_MIDDLE, a[0]);
  else if (!strcmp(name, "km.side1") && n == 1) setButton(BUTTON_BACK, a[0]);
  else if (!strcmp(name, "km.side2") && n == 1) setButton(BUTTON_FORWARD, a[0]);
  else if (!strcmp(name, "kb.down") && n == 1) { if (keyCode(a[0])) Keyboard.press(keyCode(a[0])); }
  else if (!strcmp(name, "kb.up") && n == 1) { if (keyCode(a[0])) Keyboard.release(keyCode(a[0])); }
  else if (!strcmp(name, "km.version")) Serial.println("logitech-script-bridge arduino 1.0");
}

void setup() {
  Serial.begin(115200);
  Mouse.begin();
  Keyboard.begin();
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (lineLen) {
        line[lineLen] = 0;
        handle(line);
        lineLen = 0;
      }
    } else if (lineLen < sizeof(line) - 1) {
      line[lineLen++] = c;
    }
  }
}
