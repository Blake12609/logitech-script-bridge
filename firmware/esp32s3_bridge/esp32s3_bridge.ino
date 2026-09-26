// Logitech Script Bridge - ESP32-S3 firmware
//
// Turns an ESP32-S3 into a USB mouse + keyboard that is driven by text
// commands over serial. It speaks the MAKCU mouse commands, so the PC app
// treats it the same way, plus two keyboard commands:
//
//   km.move(x,y)    relative move          km.wheel(n)    scroll
//   km.left(1|0)    km.right(1|0)          km.middle(1|0)
//   km.side1(1|0)   km.side2(1|0)          (back / forward buttons)
//   kb.down(hid)    kb.up(hid)             USB HID usage ID, e.g. 4 = 'a', 225 = left shift
//   km.version()
//
// Arduino IDE settings (Tools menu), board "ESP32S3 Dev Module":
//   USB Mode:        USB-OTG (TinyUSB)
//   USB CDC On Boot: Enabled
// Plug the board's native USB port ("USB", not "COM/UART") into the PC.
// It shows up as a serial port (for the app) and as a mouse + keyboard.

#include "USB.h"
#include "USBHIDMouse.h"
#include "USBHIDKeyboard.h"

USBHIDMouse Mouse;
USBHIDKeyboard Keyboard;

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
  else if (!strcmp(name, "km.side1") && n == 1) setButton(MOUSE_BACKWARD, a[0]);
  else if (!strcmp(name, "km.side2") && n == 1) setButton(MOUSE_FORWARD, a[0]);
  else if (!strcmp(name, "kb.down") && n == 1) Keyboard.pressRaw((uint8_t)a[0]);
  else if (!strcmp(name, "kb.up") && n == 1) Keyboard.releaseRaw((uint8_t)a[0]);
  else if (!strcmp(name, "km.version")) Serial.println("logitech-script-bridge esp32s3 1.0");
}

void setup() {
  Serial.begin(115200);
  Mouse.begin();
  Keyboard.begin();
  USB.begin();
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
