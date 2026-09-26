// The exact bytes SerialBackend puts on the wire, checked through a pseudo terminal.
#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>

#include <string>

#include "backend.h"
#include "keys.h"
#include "test.h"

namespace {

std::string readAvailable(int fd) {
    std::string out;
    char buf[512];
    pollfd p = {fd, POLLIN, 0};
    while (poll(&p, 1, 200) > 0) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        out.append(buf, n);
    }
    return out;
}

}  // namespace

TEST(serial_backend_wire_format) {
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    CHECK(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
    SerialBackend dev(ptsname(master), 115200, true);
    std::string err;
    CHECK(dev.open(err));
    dev.move(12, -7);
    dev.move(0, 0);  // no-op, nothing sent
    dev.button(Button::Left, true);
    dev.button(Button::Side2, false);
    dev.wheel(-3);
    dev.key(*findKeyByName("lshift"), true);
    dev.key(*findKeyByName("f24"), false);
    CHECK(readAvailable(master) ==
          "km.move(12,-7)\r\nkm.left(1)\r\nkm.side2(0)\r\nkm.wheel(-3)\r\nkb.down(225)\r\nkb.up(115)\r\n");
    dev.close();
    close(master);
}

TEST(serial_backend_reports_missing_port) {
    SerialBackend dev("/dev/does-not-exist", 115200, false);
    std::string err;
    CHECK(!dev.open(err));
    CHECK(err.find("does-not-exist") != std::string::npos);
    SerialBackend none("", 115200, false);
    CHECK(!none.open(err));
}
#endif
