// Config files: which script, which device, which options.
//
// Plain INI text so people can read and share them. A script stored in the
// same folder as the config, below it, or in a sibling folder (configs\ next
// to scripts\) is saved as a relative path, so a folder with the exe, configs
// and scripts can be moved anywhere.
#pragma once

#include <cstdio>
#include <map>
#include <string>

struct Config {
    std::string script;           // absolute path (UTF-8)
    std::string device = "makcu"; // DeviceInfo::key
    std::string port;
    int baud = 115200;
    bool keyFallback = true;      // type keys in software when the device can't
    std::string extraKeys = "off";  // F13-F24: "off", "mouse" (buttons 6-17) or "gkeys" (G1-G12)
    bool autoStart = false;       // start the script when the app opens
    double jitterMin = 0, jitterMax = 0;  // randomize script mouse movement: min..max pixels off (0 = off)
    std::string hotkey = "off";   // global start/stop key: "off", "f6".."f12", "pause", "scrolllock"

    bool operator==(const Config& o) const;
    bool operator!=(const Config& o) const { return !(*this == o); }
};

using ConfigExtras = std::map<std::string, std::string>;

// `extras` carries keys the app keeps alongside a config (e.g. in its own state file).
bool saveConfig(const std::string& path, const Config& c, std::string& error, const ConfigExtras* extras = nullptr);
bool loadConfig(const std::string& path, Config& c, std::string& error, ConfigExtras* extras = nullptr);

// Path helpers (UTF-8, accept both / and \).
std::string dirName(const std::string& path);
std::string fileName(const std::string& path);
bool isAbsolutePath(const std::string& path);
std::string joinPath(const std::string& dir, const std::string& name);
// Relative form of `path` as seen from `baseDir`, climbing at most `maxUp` folders ("..\");
// returned unchanged when that isn't possible (other drive, too far away).
std::string makeRelative(const std::string& path, const std::string& baseDir, int maxUp = 0);
std::string normalizePath(const std::string& path);  // resolves "." and ".."

FILE* openUtf8(const std::string& path, const char* mode);
