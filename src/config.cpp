#include "config.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
const char kSep = '\\';
#else
const char kSep = '/';
#endif

bool isSep(char c) { return c == '/' || c == '\\'; }

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
    return s.substr(a, b - a);
}

bool parseBool(const std::string& v) { return v == "1" || v == "true" || v == "yes" || v == "on"; }

double parseNumber(const std::string& v) {
    const double d = std::strtod(v.c_str(), nullptr);
    return std::isfinite(d) && d > 0 ? d : 0;
}

bool samePathChar(char a, char b) {
    if (isSep(a) && isSep(b)) return true;
#ifdef _WIN32
    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
#else
    return a == b;
#endif
}

}  // namespace

bool Config::operator==(const Config& o) const {
    return script == o.script && device == o.device && port == o.port && baud == o.baud &&
           keyFallback == o.keyFallback && extraKeys == o.extraKeys && autoStart == o.autoStart &&
           jitterMin == o.jitterMin && jitterMax == o.jitterMax && hotkey == o.hotkey;
}

FILE* openUtf8(const std::string& path, const char* mode) {
#ifdef _WIN32
    auto widen = [](const std::string& s) {
        std::wstring w(MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], static_cast<int>(w.size()));
        return w;
    };
    return _wfopen(widen(path).c_str(), widen(mode).c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

std::string dirName(const std::string& path) {
    size_t i = path.find_last_of("/\\");
    return i == std::string::npos ? std::string() : path.substr(0, i);
}

std::string fileName(const std::string& path) {
    size_t i = path.find_last_of("/\\");
    return i == std::string::npos ? path : path.substr(i + 1);
}

bool isAbsolutePath(const std::string& p) {
    if (!p.empty() && isSep(p[0])) return true;
    return p.size() >= 2 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':';
}

std::string joinPath(const std::string& dir, const std::string& name) {
    if (dir.empty() || isAbsolutePath(name)) return name;
    return isSep(dir.back()) ? dir + name : dir + kSep + name;
}

namespace {

// Splits "C:\\a\\b" into root "C:\\" and parts {a, b}; `starts` holds where each part begins.
std::vector<std::string> splitPath(const std::string& p, std::string& root, std::vector<size_t>* starts = nullptr) {
    root.clear();
    size_t i = 0;
    if (p.size() >= 2 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':') {
        root = p.substr(0, 2);
        i = 2;
    }
    while (i < p.size() && isSep(p[i])) root += p[i++];
    std::vector<std::string> parts;
    std::string cur;
    for (; i <= p.size(); i++) {
        if (i == p.size() || isSep(p[i])) {
            if (!cur.empty()) {
                parts.push_back(cur);
                if (starts) starts->push_back(i - cur.size());
            }
            cur.clear();
        } else {
            cur += p[i];
        }
    }
    return parts;
}

bool samePart(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (!samePathChar(a[i], b[i])) return false;
    return true;
}

}  // namespace

std::string normalizePath(const std::string& path) {
    std::string root;
    std::vector<std::string> out;
    for (const auto& part : splitPath(path, root)) {
        if (part == ".") continue;
        if (part == ".." && !out.empty() && out.back() != "..") {
            out.pop_back();
            continue;
        }
        out.push_back(part);
    }
    std::string r = root;
    for (char& c : r)
        if (isSep(c)) c = kSep;
    for (size_t i = 0; i < out.size(); i++) r += (i ? std::string(1, kSep) : std::string()) + out[i];
    return r;
}

std::string makeRelative(const std::string& path, const std::string& baseDir, int maxUp) {
    std::string rootP, rootB;
    std::vector<size_t> starts;
    const auto p = splitPath(path, rootP, &starts), b = splitPath(baseDir, rootB);
    if (baseDir.empty() || !samePart(rootP, rootB)) return path;
    size_t common = 0;
    while (common < p.size() && common < b.size() && samePart(p[common], b[common])) common++;
    const size_t up = b.size() - common;
    if (common >= p.size() || up > static_cast<size_t>(maxUp)) return path;
    if (up > 0 && common == 0) return path;  // never climb all the way to the drive root
    std::string r;
    for (size_t i = 0; i < up; i++) r += std::string("..") + kSep;
    return r + path.substr(starts[common]);
}

bool saveConfig(const std::string& path, const Config& c, std::string& error, const ConfigExtras* extras) {
    FILE* f = openUtf8(path, "wb");
    if (!f) {
        error = "Cannot write " + path;
        return false;
    }
    const std::string script = c.script.empty() ? "" : makeRelative(c.script, dirName(path), 1);
    std::fprintf(f,
                 "; Logitech Script Bridge configuration\r\n"
                 "[bridge]\r\n"
                 "script=%s\r\n"
                 "device=%s\r\n"
                 "port=%s\r\n"
                 "baud=%d\r\n"
                 "key_fallback=%d\r\n"
                 "extra_keys=%s\r\n"
                 "auto_start=%d\r\n"
                 "jitter_min=%g\r\n"
                 "jitter_max=%g\r\n"
                 "hotkey=%s\r\n",
                 script.c_str(), c.device.c_str(), c.port.c_str(), c.baud, c.keyFallback ? 1 : 0,
                 c.extraKeys.c_str(), c.autoStart ? 1 : 0, c.jitterMin, c.jitterMax, c.hotkey.c_str());
    if (extras)
        for (const auto& kv : *extras) std::fprintf(f, "%s=%s\r\n", kv.first.c_str(), kv.second.c_str());
    const bool ok = std::fclose(f) == 0;
    if (!ok) error = "Cannot write " + path;
    return ok;
}

bool loadConfig(const std::string& path, Config& c, std::string& error, ConfigExtras* extras) {
    FILE* f = openUtf8(path, "rb");
    if (!f) {
        error = "Cannot open " + path;
        return false;
    }
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    std::fclose(f);
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);

    Config out;
    bool hasRange = false;
    double legacyJitter = 0;  // v1.2-v1.3 stored "up to +-N px" per axis
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        if (key == "script") out.script = value.empty() ? "" : normalizePath(joinPath(dirName(path), value));
        else if (key == "device") out.device = value;
        else if (key == "port") out.port = value;
        else if (key == "baud") out.baud = std::atoi(value.c_str()) > 0 ? std::atoi(value.c_str()) : 115200;
        else if (key == "key_fallback") out.keyFallback = parseBool(value);
        else if (key == "extra_keys") out.extraKeys = value;
        else if (key == "auto_start") out.autoStart = parseBool(value);
        else if (key == "jitter_min") out.jitterMin = parseNumber(value), hasRange = true;
        else if (key == "jitter_max") out.jitterMax = parseNumber(value), hasRange = true;
        else if (key == "jitter_x" || key == "jitter_y") legacyJitter = std::max(legacyJitter, parseNumber(value));
        else if (key == "hotkey") out.hotkey = value.empty() ? "off" : value;
        else if (extras) (*extras)[key] = value;
    }
    if (!hasRange && legacyJitter > 0) out.jitterMax = legacyJitter;
    c = out;
    return true;
}
