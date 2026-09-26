// Config file save/load.
#include <cstdio>
#include <string>

#include "config.h"
#include "test.h"

#ifdef _WIN32
#include <direct.h>
#define mkdir_(p) _mkdir(p)
#else
#include <sys/stat.h>
#define mkdir_(p) mkdir(p, 0755)
#endif

namespace {

std::string tempDir() {
    static std::string dir;
    if (dir.empty()) {
        char name[64];
        std::snprintf(name, sizeof(name), "lsb_config_test_%d", static_cast<int>(std::chrono::steady_clock::now().time_since_epoch().count() % 100000));
        dir = name;
        mkdir_(dir.c_str());
        mkdir_(joinPath(dir, "scripts").c_str());
    }
    return dir;
}

std::string readAll(const std::string& path) {
    std::string s;
    if (FILE* f = openUtf8(path, "rb")) {
        char buf[1024];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
        std::fclose(f);
    }
    return s;
}

}  // namespace

TEST(config_roundtrip_with_relative_script) {
    const std::string dir = tempDir();
    Config c;
    c.script = joinPath(joinPath(dir, "scripts"), "rapid fire.lua");
    c.device = "esp32";
    c.port = "COM7";
    c.baud = 250000;
    c.keyFallback = false;
    c.extraKeys = "gkeys";
    c.autoStart = true;
    c.jitterX = 3;
    c.jitterY = 2;
    std::string err;
    const std::string path = joinPath(dir, "game.ini");
    CHECK(saveConfig(path, c, err));
    const std::string text = readAll(path);
    CHECK(text.find("script=scripts") != std::string::npos);  // stored relative to the config
    CHECK(text.find("rapid fire.lua") != std::string::npos);

    Config loaded;
    CHECK(loadConfig(path, loaded, err));
    CHECK(loaded == c);
    CHECK(loaded.script == c.script);
}

TEST(config_in_sibling_folder_uses_dotdot) {
    // exe folder layout: configs\\game.ini + scripts\\a.lua
    const std::string dir = tempDir();
    mkdir_(joinPath(dir, "configs").c_str());
    Config c;
    c.script = joinPath(joinPath(dir, "scripts"), "a.lua");
    std::string err;
    const std::string path = joinPath(joinPath(dir, "configs"), "game.ini");
    CHECK(saveConfig(path, c, err));
    CHECK(readAll(path).find(std::string("script=..") + (joinPath("x", "y")[1]) + "scripts") != std::string::npos);
    Config loaded;
    CHECK(loadConfig(path, loaded, err));
    CHECK(loaded.script == c.script);  // ".." resolved again
}

TEST(config_keeps_scripts_outside_folder_absolute) {
    const std::string dir = tempDir();
    Config c;
#ifdef _WIN32
    c.script = "D:\\other\\thing.lua";
#else
    c.script = "/opt/other/thing.lua";
#endif
    std::string err;
    const std::string path = joinPath(dir, "abs.ini");
    CHECK(saveConfig(path, c, err));
    Config loaded;
    CHECK(loadConfig(path, loaded, err));
    CHECK(loaded.script == c.script);
}

TEST(config_extras_defaults_and_bad_input) {
    const std::string dir = tempDir();
    const std::string path = joinPath(dir, "hand_written.ini");
    FILE* f = openUtf8(path, "wb");
    std::fputs("\xEF\xBB\xBF; comment\n[bridge]\n  device = kmbox-b \nbaud=abc\nkey_fallback=no\nconfig_file=x.ini\n", f);
    std::fclose(f);
    Config c;
    ConfigExtras extras;
    std::string err;
    CHECK(loadConfig(path, c, err, &extras));
    CHECK(c.device == "kmbox-b");
    CHECK(c.baud == 115200);
    CHECK(!c.keyFallback);
    CHECK(c.extraKeys == "off");
    CHECK(c.jitterX == 0 && c.jitterY == 0);
    CHECK(extras["config_file"] == "x.ini");

    Config missing;
    CHECK(!loadConfig(joinPath(dir, "does_not_exist.ini"), missing, err));
    CHECK(!err.empty());
}

TEST(path_helpers) {
    CHECK(makeRelative("C:\\bridge\\scripts\\a.lua", "C:\\bridge") == "scripts\\a.lua");
    CHECK(makeRelative("C:\\bridge2\\a.lua", "C:\\bridge") == "C:\\bridge2\\a.lua");
    CHECK(makeRelative("/home/u/bridge/a.lua", "/home/u/bridge/") == "a.lua");
    CHECK(isAbsolutePath("C:\\x") && isAbsolutePath("/x") && isAbsolutePath("\\\\server\\x"));
    CHECK(!isAbsolutePath("scripts\\x.lua"));
    CHECK(fileName("C:\\a\\b.lua") == "b.lua" && dirName("/a/b.lua") == "/a");
    CHECK(makeRelative("/t/lsb/scripts/a.lua", "/t/lsb/configs", 1) == "../scripts/a.lua" ||
          makeRelative("/t/lsb/scripts/a.lua", "/t/lsb/configs", 1) == "..\\scripts/a.lua");
    CHECK(makeRelative("/t/lsb/scripts/a.lua", "/t/lsb/configs", 0) == "/t/lsb/scripts/a.lua");
    CHECK(makeRelative("/x/a.lua", "/y", 1) == "/x/a.lua");  // would need to climb to the root
    CHECK(normalizePath("/t/lsb/configs/../scripts/./a.lua") == "/t/lsb/scripts/a.lua" ||
          normalizePath("/t/lsb/configs/../scripts/./a.lua") == "\\t\\lsb\\scripts\\a.lua");
#ifdef _WIN32
    CHECK(makeRelative("c:\\BRIDGE\\a.lua", "C:\\bridge") == "a.lua");
#endif
}
