// SerialPort for Win32 and POSIX.
#include "backend.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>

bool SerialPort::open(const std::string& port, int baud, std::string& error) {
    close();
    std::string path = port.rfind("\\\\.\\", 0) == 0 ? port : "\\\\.\\" + port;
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = "Cannot open " + port + " (error " + std::to_string(GetLastError()) +
                "). Is the device plugged in and not used by another program?";
        return false;
    }
    handle_ = h;
    if (!setBaud(baud)) {
        error = "Cannot configure " + port;
        close();
        return false;
    }
    COMMTIMEOUTS t = {};
    t.ReadIntervalTimeout = MAXDWORD;
    t.ReadTotalTimeoutMultiplier = MAXDWORD;
    t.ReadTotalTimeoutConstant = 50;  // read() returns after at most 50 ms
    t.WriteTotalTimeoutConstant = 1000;
    SetCommTimeouts(h, &t);
    return true;
}

bool SerialPort::setBaud(int baud) {
    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle_, &dcb)) return false;
    dcb.BaudRate = static_cast<DWORD>(baud);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    return SetCommState(handle_, &dcb) != 0;
}

void SerialPort::close() {
    if (handle_) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }
}

bool SerialPort::isOpen() const { return handle_ != nullptr; }

bool SerialPort::write(const std::string& data) {
    if (!handle_) return false;
    DWORD written = 0;
    return WriteFile(handle_, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
           written == data.size();
}

int SerialPort::read(char* buf, int size) {
    if (!handle_) return -1;
    DWORD got = 0;
    if (!ReadFile(handle_, buf, static_cast<DWORD>(size), &got, nullptr)) return -1;
    return static_cast<int>(got);
}

std::vector<std::string> listSerialPorts() {
    std::vector<std::string> ports;
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &key) != ERROR_SUCCESS)
        return ports;
    for (DWORD i = 0;; i++) {
        char name[256], value[256];
        DWORD nameLen = sizeof(name), valueLen = sizeof(value), type = 0;
        if (RegEnumValueA(key, i, name, &nameLen, nullptr, &type, reinterpret_cast<BYTE*>(value), &valueLen) !=
            ERROR_SUCCESS)
            break;
        if (type == REG_SZ) ports.emplace_back(value, strnlen(value, valueLen));
    }
    RegCloseKey(key);
    return ports;
}

#else  // POSIX

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>

static speed_t toSpeed(int baud) {
    switch (baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 230400: return B230400;
#ifdef B4000000
        case 4000000: return B4000000;
#endif
        default: return B115200;
    }
}

bool SerialPort::open(const std::string& port, int baud, std::string& error) {
    close();
    fd_ = ::open(port.c_str(), O_RDWR | O_NOCTTY);
    if (fd_ < 0) {
        error = "Cannot open " + port + ": " + std::strerror(errno);
        return false;
    }
    if (!setBaud(baud)) {
        error = "Cannot configure " + port;
        close();
        return false;
    }
    return true;
}

bool SerialPort::setBaud(int baud) {
    termios tio = {};
    if (tcgetattr(fd_, &tio) != 0) return false;
    cfmakeraw(&tio);
    cfsetispeed(&tio, toSpeed(baud));
    cfsetospeed(&tio, toSpeed(baud));
    tio.c_cflag |= CLOCAL | CREAD;
    return tcsetattr(fd_, TCSANOW, &tio) == 0;
}

void SerialPort::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool SerialPort::isOpen() const { return fd_ >= 0; }

bool SerialPort::write(const std::string& data) {
    return fd_ >= 0 && ::write(fd_, data.data(), data.size()) == static_cast<ssize_t>(data.size());
}

int SerialPort::read(char* buf, int size) {
    if (fd_ < 0) return -1;
    pollfd p = {fd_, POLLIN, 0};
    int r = poll(&p, 1, 50);
    if (r <= 0) return r;
    return static_cast<int>(::read(fd_, buf, size));
}

std::vector<std::string> listSerialPorts() {
    std::vector<std::string> ports;
    if (DIR* d = opendir("/dev")) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n.rfind("ttyACM", 0) == 0 || n.rfind("ttyUSB", 0) == 0 || n.rfind("cu.usb", 0) == 0)
                ports.push_back("/dev/" + n);
        }
        closedir(d);
    }
    std::sort(ports.begin(), ports.end());
    return ports;
}

#endif
