// The window: a small custom-drawn dark UI on plain Win32 + GDI+.
// App state is kept in LogitechScriptBridge.ini next to the exe (portable);
// named configs can be saved/loaded anywhere (default: configs\ next to the exe).
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <objidl.h>  // GDI+ needs COM declarations that WIN32_LEAN_AND_MEAN leaves out
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "backend.h"
#include "config.h"
#include "engine.h"
#include "platform_win.h"

namespace {

// ------------------------------------------------------------------ theme

namespace Color {
const COLORREF bg = RGB(0x15, 0x17, 0x1c);
const COLORREF card = RGB(0x1e, 0x21, 0x28);
const COLORREF field = RGB(0x27, 0x2b, 0x33);
const COLORREF fieldHover = RGB(0x2f, 0x34, 0x3e);
const COLORREF border = RGB(0x32, 0x37, 0x41);
const COLORREF borderHover = RGB(0x46, 0x4d, 0x5a);
const COLORREF text = RGB(0xe8, 0xea, 0xed);
const COLORREF muted = RGB(0x8d, 0x93, 0x9e);
const COLORREF accent = RGB(0x4f, 0x8c, 0xff);
const COLORREF accentHover = RGB(0x6c, 0xa0, 0xff);
const COLORREF danger = RGB(0xe5, 0x48, 0x4d);
const COLORREF dangerHover = RGB(0xf0, 0x5f, 0x64);
const COLORREF success = RGB(0x3f, 0xb9, 0x50);
const COLORREF logBg = RGB(0x12, 0x14, 0x18);
const COLORREF white = RGB(0xff, 0xff, 0xff);
}  // namespace Color

COLORREF mix(COLORREF a, COLORREF b, double t) {
    auto ch = [&](int shift) {
        const int x = (a >> shift) & 0xFF, y = (b >> shift) & 0xFF;
        return static_cast<COLORREF>(x + (y - x) * t) << shift;
    };
    return ch(0) | ch(8) | ch(16);
}

Gdiplus::Color gp(COLORREF c) { return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)); }

void roundRect(HDC dc, RECT r, int radius, COLORREF fill, COLORREF line = CLR_INVALID, int lineWidth = 1) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float x = static_cast<float>(r.left) + 0.5f, y = static_cast<float>(r.top) + 0.5f;
    const float w = static_cast<float>(r.right - r.left) - 1.0f, h = static_cast<float>(r.bottom - r.top) - 1.0f;
    const float d = std::min(static_cast<float>(radius * 2), std::min(w, h));
    Gdiplus::GraphicsPath path;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
    if (fill != CLR_INVALID) {
        Gdiplus::SolidBrush brush(gp(fill));
        g.FillPath(&brush, &path);
    }
    if (line != CLR_INVALID) {
        Gdiplus::Pen pen(gp(line), static_cast<float>(lineWidth));
        g.DrawPath(&pen, &path);
    }
}

void polygon(HDC dc, const std::vector<Gdiplus::PointF>& pts, COLORREF fill) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush brush(gp(fill));
    g.FillPolygon(&brush, pts.data(), static_cast<INT>(pts.size()));
}

void chevron(HDC dc, int cx, int cy, int size, float stroke, COLORREF c) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::Pen pen(gp(c), stroke);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    const float s = size / 2.0f;
    Gdiplus::PointF pts[] = {{cx - s, cy - s / 4}, {static_cast<float>(cx), cy + s / 2}, {cx + s, cy - s / 4}};
    g.DrawLines(&pen, pts, 3);
}

void circle(HDC dc, float cx, float cy, float r, COLORREF c) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush brush(gp(c));
    g.FillEllipse(&brush, cx - r, cy - r, r * 2, r * 2);
}

void text(HDC dc, const std::wstring& s, RECT r, HFONT font, COLORREF c, UINT flags = DT_LEFT | DT_VCENTER) {
    SelectObject(dc, font);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s.c_str(), -1, &r, flags | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}

int textWidth(HDC dc, const std::wstring& s, HFONT font) {
    SelectObject(dc, font);
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.c_str(), static_cast<int>(s.size()), &sz);
    return sz.cx;
}

// ------------------------------------------------------------------ controls

enum Id {
    IDC_START = 100, IDC_STOP, IDC_OPEN, IDC_LOADCFG, IDC_SAVECFG, IDC_SAVEAS, IDC_RELOAD, IDC_EDIT,
    IDC_SCRIPT, IDC_SCRIPT_MENU, IDC_BROWSE, IDC_DEVICE, IDC_PORT, IDC_PORT_MENU, IDC_BAUD, IDC_TEST,
    IDC_KEYFALLBACK, IDC_EXTRA, IDC_AUTOSTART, IDC_JITTER_X, IDC_JITTER_Y, IDC_CLEAR, IDC_LOG,
};

enum class Role { Primary, Danger, Secondary, Dropdown, Chevron, Toggle };

const UINT WM_APP_CLEARLOG = WM_APP + 1;
const UINT_PTR kLogTimer = 1;

const char* const kExtraKeys[] = {"off", "mouse", "gkeys"};
const wchar_t* const kExtraLabels[] = {L"Off (F13–F24 stay normal keys)", L"Extra mouse buttons 6–17",
                                       L"G-keys G1–G12"};

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

struct Card {
    RECT rect;
    std::wstring caption;
};

struct Label {
    RECT rect;
    std::wstring text;
};

struct App {
    HWND wnd = nullptr;
    int dpi = 96;
    HFONT font = nullptr, fontBold = nullptr, fontSmall = nullptr, fontTitle = nullptr, fontMono = nullptr;
    HBRUSH fieldBrush = nullptr, logBrush = nullptr;

    std::string exeDir, statePath, configPath;
    Config savedConfig;  // contents of configPath, to show "unsaved changes"
    int device = 0, extraKeys = 0;
    bool keyFallback = true, autoStart = false;
    enum class Status { Stopped, Running, Error } status = Status::Stopped;

    WinInput input;
    std::unique_ptr<Backend> backend, fallback;
    std::unique_ptr<Engine> engine;

    std::mutex logMu;
    std::string pendingLog;

    std::map<int, Role> roles;
    std::map<int, RECT> fields;  // painted field boxes around edits (by edit id)
    std::vector<Card> cards;
    std::vector<Label> labels;
    RECT header{}, hintRect{}, pillRect{};
    HWND hover = nullptr;
    HACCEL accel = nullptr;

    HWND item(int id) const { return GetDlgItem(wnd, id); }
    int S(int v) const { return MulDiv(v, dpi, 96); }
    bool running() const { return engine != nullptr; }
    void log(const std::string& s) {
        std::lock_guard<std::mutex> lock(logMu);
        pendingLog += s;
    }
} app;

std::wstring getText(int id) {
    HWND h = app.item(id);
    std::wstring s(GetWindowTextLengthW(h), L'\0');
    GetWindowTextW(h, &s[0], static_cast<int>(s.size()) + 1);
    return s;
}

// ------------------------------------------------------------------ config <-> UI

Config currentConfig() {
    Config c;
    c.script = narrow(getText(IDC_SCRIPT));
    c.device = kDevices[app.device].key;
    c.port = narrow(getText(IDC_PORT));
    c.baud = std::max(1, _wtoi(getText(IDC_BAUD).c_str()));
    if (getText(IDC_BAUD).empty()) c.baud = 115200;
    c.keyFallback = app.keyFallback;
    c.extraKeys = kExtraKeys[app.extraKeys];
    c.autoStart = app.autoStart;
    c.jitterX = std::max(0, _wtoi(getText(IDC_JITTER_X).c_str()));
    c.jitterY = std::max(0, _wtoi(getText(IDC_JITTER_Y).c_str()));
    return c;
}

void refreshChrome();

void applyConfig(const Config& c) {
    SetWindowTextW(app.item(IDC_SCRIPT), widen(c.script).c_str());
    const DeviceInfo* d = findDevice(c.device);
    app.device = d ? static_cast<int>(d - kDevices) : 0;
    SetWindowTextW(app.item(IDC_PORT), widen(c.port).c_str());
    SetWindowTextW(app.item(IDC_BAUD), std::to_wstring(c.baud).c_str());
    app.keyFallback = c.keyFallback;
    app.autoStart = c.autoStart;
    SetWindowTextW(app.item(IDC_JITTER_X), std::to_wstring(c.jitterX).c_str());
    SetWindowTextW(app.item(IDC_JITTER_Y), std::to_wstring(c.jitterY).c_str());
    app.extraKeys = 0;
    for (int i = 0; i < 3; i++)
        if (c.extraKeys == kExtraKeys[i]) app.extraKeys = i;
    for (int id : {IDC_DEVICE, IDC_KEYFALLBACK, IDC_AUTOSTART, IDC_EXTRA}) InvalidateRect(app.item(id), nullptr, TRUE);
    refreshChrome();
}

void saveState() {
    ConfigExtras extras;
    if (!app.configPath.empty()) extras["config_file"] = makeRelative(app.configPath, app.exeDir);
    std::string err;
    saveConfig(app.statePath, currentConfig(), err, &extras);
}

bool modified() { return !app.configPath.empty() && currentConfig() != app.savedConfig; }

// Title, subtitle, device hint and enabled states follow the current values.
void refreshChrome() {
    std::wstring title = L"Logitech Script Bridge";
    if (!app.configPath.empty()) title += L" – " + widen(fileName(app.configPath)) + (modified() ? L"*" : L"");
    SetWindowTextW(app.wnd, title.c_str());

    const bool run = app.running(), serial = kDevices[app.device].serial;
    EnableWindow(app.item(IDC_START), !run);
    EnableWindow(app.item(IDC_STOP), run);
    EnableWindow(app.item(IDC_RELOAD), run);
    for (int id : {IDC_SCRIPT, IDC_SCRIPT_MENU, IDC_BROWSE, IDC_OPEN, IDC_LOADCFG, IDC_DEVICE, IDC_EXTRA,
                   IDC_KEYFALLBACK, IDC_AUTOSTART, IDC_JITTER_X, IDC_JITTER_Y})
        EnableWindow(app.item(id), !run);
    for (int id : {IDC_PORT, IDC_PORT_MENU, IDC_BAUD}) EnableWindow(app.item(id), !run && serial);
    EnableWindow(app.item(IDC_TEST), !run);
    EnableWindow(app.item(IDC_KEYFALLBACK), !run && !kDevices[app.device].keyboard);
    InvalidateRect(app.wnd, &app.header, FALSE);
    InvalidateRect(app.wnd, &app.hintRect, FALSE);
    for (auto& f : app.fields) InvalidateRect(app.wnd, &f.second, FALSE);
}

// ------------------------------------------------------------------ log

void flushLog() {
    std::string chunk;
    {
        std::lock_guard<std::mutex> lock(app.logMu);
        chunk.swap(app.pendingLog);
    }
    if (chunk.empty()) return;
    std::string crlf;
    for (size_t i = 0; i < chunk.size(); i++) {
        if (chunk[i] == '\n' && (i == 0 || chunk[i - 1] != '\r')) crlf += '\r';
        crlf += chunk[i];
    }
    HWND box = app.item(IDC_LOG);
    int len = GetWindowTextLengthW(box);
    if (len > 400000) {  // keep the log from growing forever
        SendMessageW(box, EM_SETSEL, 0, len / 2);
        SendMessageW(box, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
        len = GetWindowTextLengthW(box);
    }
    SendMessageW(box, EM_SETSEL, len, len);
    SendMessageW(box, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(widen(crlf).c_str()));
}

void clearLog() {
    flushLog();
    SetWindowTextW(app.item(IDC_LOG), L"");
}

void dryRunLog(void*, const std::string& s) { app.log(s); }

// ------------------------------------------------------------------ dialogs & menus

void errorBox(const std::wstring& title, const std::string& msg) {
    MessageBoxW(app.wnd, widen(msg).c_str(), title.c_str(), MB_ICONERROR | MB_OK);
}

std::string ensureDir(const std::string& name) {
    const std::string dir = joinPath(app.exeDir, name);
    CreateDirectoryW(widen(dir).c_str(), nullptr);
    return dir;
}

std::string fileDialog(bool save, const wchar_t* filter, const std::string& initialDir, const std::string& initialName,
                       const wchar_t* defExt) {
    wchar_t file[MAX_PATH] = L"";
    lstrcpynW(file, widen(initialName).c_str(), MAX_PATH);
    const std::wstring dir = widen(initialDir);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app.wnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.lpstrDefExt = defExt;
    ofn.Flags = save ? OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST : OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    return ok ? narrow(file) : std::string();
}

// Pops up a menu under `anchor` (a control or a field box) and returns the chosen index or -1.
int popupMenu(const RECT& anchorClient, const std::vector<std::wstring>& items, int checked,
              const std::vector<bool>* enabled = nullptr) {
    HMENU menu = CreatePopupMenu();
    for (size_t i = 0; i < items.size(); i++) {
        if (items[i].empty()) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            continue;
        }
        UINT flags = MF_STRING;
        if (static_cast<int>(i) == checked) flags |= MF_CHECKED;
        if (enabled && !(*enabled)[i]) flags |= MF_GRAYED;
        AppendMenuW(menu, flags, i + 1, items[i].c_str());
    }
    POINT pt = {anchorClient.left, anchorClient.bottom + app.S(4)};
    ClientToScreen(app.wnd, &pt);
    const int cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY, pt.x, pt.y,
                                     app.wnd, nullptr);
    DestroyMenu(menu);
    return cmd - 1;
}

RECT clientRectOf(int id) {
    RECT r{};
    if (!GetWindowRect(app.item(id), &r)) return r;
    MapWindowPoints(nullptr, app.wnd, reinterpret_cast<POINT*>(&r), 2);
    return r;
}

std::vector<std::string> listLua(const std::string& dir) {
    std::vector<std::string> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(widen(joinPath(dir, "*.lua")).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(joinPath(dir, narrow(fd.cFileName)));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

// ------------------------------------------------------------------ actions

void closeDevices() {
    if (app.backend) app.backend->close();
    if (app.fallback) app.fallback->close();
    app.backend.reset();
    app.fallback.reset();
}

void stopScript() {
    app.input.stopHook();
    if (app.engine) app.engine->stop();
    app.engine.reset();
    closeDevices();
    flushLog();
    if (app.status == App::Status::Running) app.status = App::Status::Stopped;
    refreshChrome();
}

bool readFile(const std::string& path, std::string& out) {
    FILE* f = openUtf8(path, "rb");
    if (!f) return false;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    if (out.compare(0, 3, "\xEF\xBB\xBF") == 0) out.erase(0, 3);  // UTF-8 BOM
    return true;
}

std::unique_ptr<Backend> openDevice(const Config& c, std::string& err) {
    const DeviceInfo& info = kDevices[app.device];
    auto dev = createBackend(info.id, c.port, c.baud);
    if (!dev) {
        err = "This device is not available on this system.";
        return nullptr;
    }
    if (auto* dry = dynamic_cast<DryRunBackend*>(dev.get())) dry->setLogger(dryRunLog, nullptr);
    if (!dev->open(err)) return nullptr;
    return dev;
}

void startScript() {
    const Config c = currentConfig();
    saveState();
    std::string source, err;
    if (c.script.empty() || !readFile(c.script, source)) {
        errorBox(L"Script", "Cannot open the script file. Pick a .lua file first.");
        return;
    }
    const DeviceInfo& info = kDevices[app.device];
    app.backend = openDevice(c, err);
    if (!app.backend) {
        errorBox(L"Device", err);
        return;
    }
    if (!app.backend->supportsKeyboard() && c.keyFallback) app.fallback = createSoftwareBackend();

    app.engine = std::make_unique<Engine>(*app.backend, app.input, [](const std::string& s) { app.log(s); },
                                          app.fallback.get());
    app.engine->onClearLog = [] { PostMessageW(app.wnd, WM_APP_CLEARLOG, 0, 0); };
    app.engine->setJitter(c.jitterX, c.jitterY);

    app.log(std::string("\nStarting ") + fileName(c.script) + " on " + info.label + "\n");
    if (!app.engine->start(source, fileName(c.script), err)) {
        app.engine.reset();
        closeDevices();
        app.log("Could not load script:\n" + err + "\n");
        app.status = App::Status::Error;
        flushLog();
        refreshChrome();
        return;
    }
    if (!app.input.startHook(app.engine.get(), c.extraKeys, err)) {
        stopScript();
        errorBox(L"Input", err);
        return;
    }
    if (c.jitterX || c.jitterY)
        app.log("Randomizing mouse movement by up to \u00B1" + std::to_string(c.jitterX) + " px X / \u00B1" +
                std::to_string(c.jitterY) + " px Y.\n");
    if (c.extraKeys == "mouse") app.log("F13–F24 act as mouse buttons 6–17.\n");
    if (c.extraKeys == "gkeys") app.log("F13–F24 act as G-keys G1–G12.\n");
    app.status = App::Status::Running;
    refreshChrome();
}

void testDevice() {
    const Config c = currentConfig();
    std::string err;
    auto dev = openDevice(c, err);
    if (!dev) {
        errorBox(L"Device test", err);
        return;
    }
    // A small visible wiggle proves the whole path works.
    for (int i = 0; i < 2; i++) {
        dev->move(app.S(60), 0);
        Sleep(150);
        dev->move(-app.S(60), 0);
        Sleep(150);
    }
    dev->close();
    app.log(std::string("Test: ") + kDevices[app.device].label +
            " moved the pointer right and back twice. Didn't move? Check the port and firmware.\n");
}

void loadConfigFile(const std::string& path) {
    Config c;
    std::string err;
    if (!loadConfig(path, c, err)) {
        errorBox(L"Load config", err);
        return;
    }
    app.configPath = path;
    app.savedConfig = c;
    applyConfig(c);
    app.log("Loaded config " + fileName(path) + "\n");
    saveState();
}

void saveConfigAs() {
    std::string name = app.configPath.empty() ? fileName(narrow(getText(IDC_SCRIPT))) : fileName(app.configPath);
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".lua") == 0) name = name.substr(0, name.size() - 4);
    if (name.empty()) name = "my config";
    const std::string dir = app.configPath.empty() ? ensureDir("configs") : dirName(app.configPath);
    const std::string path = fileDialog(true, L"Bridge config (*.ini)\0*.ini\0All files (*.*)\0*.*\0", dir,
                                        name.size() > 4 && name.compare(name.size() - 4, 4, ".ini") == 0 ? name : name + ".ini",
                                        L"ini");
    if (path.empty()) return;
    std::string err;
    const Config c = currentConfig();
    if (!saveConfig(path, c, err)) {
        errorBox(L"Save config", err);
        return;
    }
    app.configPath = path;
    app.savedConfig = c;
    app.log("Saved config " + fileName(path) + "\n");
    saveState();
    refreshChrome();
}

void saveConfigFile() {
    if (app.configPath.empty()) return saveConfigAs();
    std::string err;
    const Config c = currentConfig();
    if (!saveConfig(app.configPath, c, err)) {
        errorBox(L"Save config", err);
        return;
    }
    app.savedConfig = c;
    app.log("Saved config " + fileName(app.configPath) + "\n");
    saveState();
    refreshChrome();
}

void openScript() {
    const std::string cur = narrow(getText(IDC_SCRIPT));
    const std::string path = fileDialog(false, L"Lua scripts (*.lua)\0*.lua\0All files (*.*)\0*.*\0",
                                        cur.empty() ? ensureDir("scripts") : dirName(cur), "", L"lua");
    if (!path.empty()) SetWindowTextW(app.item(IDC_SCRIPT), widen(path).c_str());
}

void editScript() {
    const std::wstring path = getText(IDC_SCRIPT);
    if (path.empty()) return openScript();
    if (reinterpret_cast<INT_PTR>(ShellExecuteW(app.wnd, L"edit", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
        ShellExecuteW(app.wnd, L"open", L"notepad.exe", (L"\"" + path + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
}

void scriptMenu() {
    std::vector<std::wstring> items;
    std::vector<std::string> paths;
    for (const char* folder : {"scripts", "examples"}) {
        const auto files = listLua(joinPath(app.exeDir, folder));
        if (!files.empty() && !items.empty()) {
            items.push_back(L"");
            paths.push_back("");
        }
        for (const auto& f : files) {
            items.push_back(widen(std::string(folder) + "\\" + fileName(f)));
            paths.push_back(f);
        }
    }
    if (!items.empty()) {
        items.push_back(L"");
        paths.push_back("");
    }
    items.push_back(L"Open the scripts folder");
    paths.push_back("*folder");

    int checked = -1;
    for (size_t i = 0; i < paths.size(); i++)
        if (!paths[i].empty() && widen(paths[i]) == getText(IDC_SCRIPT)) checked = static_cast<int>(i);
    const int i = popupMenu(app.fields[IDC_SCRIPT], items, checked);
    if (i < 0) return;
    if (paths[i] == "*folder")
        ShellExecuteW(app.wnd, L"open", widen(ensureDir("scripts")).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    else if (!paths[i].empty())
        SetWindowTextW(app.item(IDC_SCRIPT), widen(paths[i]).c_str());
}

void portMenu() {
    const auto ports = listSerialPorts();
    std::vector<std::wstring> items;
    for (const auto& p : ports) items.push_back(widen(p));
    std::vector<bool> enabled(items.size(), true);
    if (items.empty()) {
        items.push_back(L"No serial ports found – is the device plugged in?");
        enabled.push_back(false);
    }
    int checked = -1;
    for (size_t i = 0; i < ports.size(); i++)
        if (widen(ports[i]) == getText(IDC_PORT)) checked = static_cast<int>(i);
    const int i = popupMenu(app.fields[IDC_PORT], items, checked, &enabled);
    if (i >= 0 && i < static_cast<int>(ports.size())) SetWindowTextW(app.item(IDC_PORT), items[i].c_str());
}

void deviceMenu() {
    std::vector<std::wstring> items;
    for (const auto& d : kDevices) items.push_back(widen(d.label));
    const int i = popupMenu(clientRectOf(IDC_DEVICE), items, app.device);
    if (i < 0) return;
    app.device = i;
    InvalidateRect(app.item(IDC_DEVICE), nullptr, TRUE);
    refreshChrome();
}

void extraMenu() {
    const int i = popupMenu(clientRectOf(IDC_EXTRA), {kExtraLabels[0], kExtraLabels[1], kExtraLabels[2]}, app.extraKeys);
    if (i < 0) return;
    app.extraKeys = i;
    InvalidateRect(app.item(IDC_EXTRA), nullptr, TRUE);
    refreshChrome();
}

// ------------------------------------------------------------------ drawing

void drawButton(const DRAWITEMSTRUCT* di) {
    HDC dc = di->hDC;
    RECT r = di->rcItem;
    const int id = static_cast<int>(di->CtlID);
    const Role role = app.roles[id];
    const bool disabled = di->itemState & ODS_DISABLED, pressed = di->itemState & ODS_SELECTED;
    const bool hover = di->hwndItem == app.hover && !disabled;
    const bool focus = (di->itemState & ODS_FOCUS) && !(di->itemState & ODS_NOFOCUSRECT);

    // what's behind the control
    COLORREF surface = Color::card;
    if (id == IDC_START || id == IDC_STOP || id == IDC_OPEN || id == IDC_LOADCFG || id == IDC_SAVECFG ||
        id == IDC_SAVEAS || id == IDC_RELOAD || id == IDC_EDIT)
        surface = Color::bg;
    if (role == Role::Chevron) surface = Color::field;

    // paint off-screen to avoid flicker
    const int w = r.right - r.left, h = r.bottom - r.top;
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    RECT rc = {0, 0, w, h};
    HBRUSH sb = CreateSolidBrush(surface);
    FillRect(mem, &rc, sb);
    DeleteObject(sb);

    wchar_t label[128] = L"";
    GetWindowTextW(di->hwndItem, label, 128);
    const int radius = app.S(7);

    switch (role) {
        case Role::Primary:
        case Role::Danger: {
            COLORREF fill = role == Role::Primary ? (hover ? Color::accentHover : Color::accent)
                                                  : (hover ? Color::dangerHover : Color::danger);
            if (pressed) fill = mix(fill, Color::bg, 0.25);
            if (disabled) fill = Color::field;
            roundRect(mem, rc, radius, fill);
            const COLORREF fg = disabled ? Color::muted : Color::white;
            const int tw = textWidth(mem, label, app.fontBold), icon = app.S(10), gap = app.S(8);
            const int x = (w - tw - icon - gap) / 2, cy = h / 2;
            if (role == Role::Primary) {
                polygon(mem, {{static_cast<float>(x), cy - icon / 2.0f}, {static_cast<float>(x + icon), static_cast<float>(cy)},
                              {static_cast<float>(x), cy + icon / 2.0f}}, fg);
            } else {
                RECT sq = {x, cy - icon / 2, x + icon, cy + icon / 2};
                roundRect(mem, sq, app.S(2), fg);
            }
            RECT tr = {x + icon + gap, 0, w, h};
            text(mem, label, tr, app.fontBold, fg);
            break;
        }
        case Role::Secondary: {
            COLORREF fill = hover ? Color::fieldHover : Color::field;
            if (pressed) fill = Color::border;
            roundRect(mem, rc, radius, fill, hover ? Color::borderHover : Color::border);
            text(mem, label, rc, app.font, disabled ? Color::muted : Color::text, DT_CENTER | DT_VCENTER);
            break;
        }
        case Role::Dropdown: {
            roundRect(mem, rc, radius, hover ? Color::fieldHover : Color::field,
                      focus ? Color::accent : hover ? Color::borderHover : Color::border);
            std::wstring value = id == IDC_DEVICE ? widen(kDevices[app.device].label) : kExtraLabels[app.extraKeys];
            RECT tr = {app.S(12), 0, w - app.S(34), h};
            text(mem, value, tr, app.font, disabled ? Color::muted : Color::text);
            chevron(mem, w - app.S(18), h / 2, app.S(10), app.dpi / 96.0f * 1.6f, Color::muted);
            break;
        }
        case Role::Chevron: {
            if (hover || pressed) {
                RECT hr = {app.S(2), app.S(2), w - app.S(2), h - app.S(2)};
                roundRect(mem, hr, app.S(5), pressed ? Color::border : Color::fieldHover);
            }
            chevron(mem, w / 2, h / 2, app.S(10), app.dpi / 96.0f * 1.6f, disabled ? Color::border : Color::muted);
            break;
        }
        case Role::Toggle: {
            const bool on = id == IDC_KEYFALLBACK ? app.keyFallback : app.autoStart;
            const int tw = app.S(38), th = app.S(22), ty = (h - th) / 2;
            RECT track = {0, ty, tw, ty + th};
            COLORREF trackColor = on ? (hover ? Color::accentHover : Color::accent) : (hover ? Color::borderHover : Color::border);
            if (disabled) trackColor = mix(trackColor, Color::card, 0.55);
            roundRect(mem, track, th / 2, trackColor);
            const float knobR = th / 2.0f - app.S(4);
            circle(mem, on ? tw - th / 2.0f : th / 2.0f, ty + th / 2.0f, knobR, disabled ? Color::muted : Color::white);
            if (focus) roundRect(mem, track, th / 2, trackColor, Color::accentHover);
            RECT tr = {tw + app.S(12), 0, w, h};
            text(mem, label, tr, app.font, disabled ? Color::muted : Color::text);
            break;
        }
    }
    if (focus && (role == Role::Primary || role == Role::Danger || role == Role::Secondary)) {
        roundRect(mem, rc, radius, CLR_INVALID, Color::accentHover);
    }
    BitBlt(dc, r.left, r.top, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

void paintWindow(HDC target) {
    RECT client;
    GetClientRect(app.wnd, &client);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, client.right, client.bottom);
    HGDIOBJ old = SelectObject(dc, bmp);
    HBRUSH bg = CreateSolidBrush(Color::bg);
    FillRect(dc, &client, bg);
    DeleteObject(bg);

    // header
    const int m = app.S(20);
    RECT t = {m, app.S(14), client.right / 2, app.S(44)};
    text(dc, L"Logitech Script Bridge", t, app.fontTitle, Color::text);
    std::wstring sub = L"Run Logitech G HUB Lua scripts on any mouse";
    if (!app.configPath.empty())
        sub = L"Config: " + widen(fileName(app.configPath)) + (modified() ? L"  •  unsaved changes" : L"");
    RECT st = {m, app.S(44), client.right - app.S(260), app.S(64)};
    text(dc, sub, st, app.fontSmall, modified() ? mix(Color::muted, Color::accent, 0.6) : Color::muted);

    // status pill
    const wchar_t* status = app.status == App::Status::Running ? L"Running"
                            : app.status == App::Status::Error ? L"Script error" : L"Stopped";
    const COLORREF sc = app.status == App::Status::Running ? Color::success
                        : app.status == App::Status::Error ? Color::danger : Color::muted;
    const int pw = textWidth(dc, status, app.fontSmall) + app.S(34);
    RECT stop = clientRectOf(IDC_STOP);
    app.pillRect = {stop.left - app.S(12) - pw, stop.top + app.S(6), stop.left - app.S(12), stop.bottom - app.S(6)};
    roundRect(dc, app.pillRect, (app.pillRect.bottom - app.pillRect.top) / 2, mix(Color::bg, sc, 0.16), mix(Color::bg, sc, 0.35));
    circle(dc, static_cast<float>(app.pillRect.left + app.S(14)), (app.pillRect.top + app.pillRect.bottom) / 2.0f,
           static_cast<float>(app.S(4)), sc);
    RECT pt = {app.pillRect.left + app.S(24), app.pillRect.top, app.pillRect.right, app.pillRect.bottom};
    text(dc, status, pt, app.fontSmall, mix(sc, Color::text, 0.35));

    // cards
    for (const auto& c : app.cards) {
        roundRect(dc, c.rect, app.S(10), Color::card, Color::border);
        RECT cr = {c.rect.left + app.S(16), c.rect.top + app.S(12), c.rect.right - app.S(16), c.rect.top + app.S(30)};
        text(dc, c.caption, cr, app.fontSmall, Color::muted);
    }
    for (const auto& l : app.labels) text(dc, l.text, l.rect, app.fontSmall, Color::muted);

    // fields behind edit controls
    HWND focus = GetFocus();
    for (const auto& f : app.fields) {
        const bool isLog = f.first == IDC_LOG;
        const bool focused = focus == app.item(f.first);
        roundRect(dc, f.second, app.S(7), isLog ? Color::logBg : Color::field,
                  focused && !isLog ? Color::accent : Color::border);
    }
    // device hint
    text(dc, widen(kDevices[app.device].hint), app.hintRect, app.fontSmall, Color::muted);

    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
}

// ------------------------------------------------------------------ layout

void layout() {
    RECT rc;
    GetClientRect(app.wnd, &rc);
    const int W = rc.right, H = rc.bottom, m = app.S(20), pad = app.S(16), fh = app.S(36), gap = app.S(8);
    auto place = [](int id, int x, int y, int w, int h) { MoveWindow(app.item(id), x, y, w, h, FALSE); };
    // an edit inside a painted field box, text vertically centred
    auto field = [&](int id, int x, int y, int w, int rightInset = 0) {
        app.fields[id] = {x, y, x + w, y + fh};
        const int th = app.S(18);
        place(id, x + app.S(12), y + (fh - th) / 2, w - app.S(24) - rightInset, th);
    };
    app.cards.clear();
    app.labels.clear();

    // header
    app.header = {0, 0, W, app.S(70)};
    place(IDC_START, W - m - app.S(112), app.S(18), app.S(112), app.S(38));
    place(IDC_STOP, W - m - app.S(112) - gap - app.S(96), app.S(18), app.S(96), app.S(38));

    // toolbar
    int x = m, y = app.S(76);
    const int bh = app.S(32);
    const struct { int id, width; } toolbar[] = {{IDC_OPEN, 112}, {IDC_LOADCFG, 112}, {IDC_SAVECFG, 112}, {IDC_SAVEAS, 96}};
    for (const auto& b : toolbar) {
        place(b.id, x, y, app.S(b.width), bh);
        x += app.S(b.width) + gap;
    }
    place(IDC_RELOAD, W - m - app.S(96), y, app.S(96), bh);
    place(IDC_EDIT, W - m - app.S(96) - gap - app.S(104), y, app.S(104), bh);

    // script card
    y = app.S(122);
    app.cards.push_back({{m, y, W - m, y + app.S(88)}, L"SCRIPT"});
    const int browseW = app.S(96);
    field(IDC_SCRIPT, m + pad, y + app.S(38), W - 2 * m - 2 * pad - browseW - gap, app.S(34));
    RECT sf = app.fields[IDC_SCRIPT];
    place(IDC_SCRIPT_MENU, sf.right - app.S(36), sf.top + app.S(3), app.S(33), fh - app.S(6));
    place(IDC_BROWSE, sf.right + gap, sf.top, browseW, fh);

    // device + options cards
    y += app.S(88) + app.S(12);
    const int cardH = app.S(244), half = (W - 2 * m - app.S(12)) / 2;
    const int lx = m, rx = m + half + app.S(12);
    app.cards.push_back({{lx, y, lx + half, y + cardH}, L"OUTPUT DEVICE"});
    app.cards.push_back({{rx, y, rx + half, y + cardH}, L"OPTIONS"});

    const int inner = half - 2 * pad;
    place(IDC_DEVICE, lx + pad, y + app.S(38), inner, fh);
    app.hintRect = {lx + pad + app.S(2), y + app.S(78), lx + half - pad, y + app.S(96)};
    const int baudW = app.S(96), testW = app.S(72);
    const int portW = inner - baudW - testW - 2 * gap;
    app.labels.push_back({{lx + pad + app.S(2), y + app.S(104), lx + pad + portW, y + app.S(122)}, L"Port"});
    app.labels.push_back({{lx + pad + portW + gap + app.S(2), y + app.S(104), lx + pad + portW + gap + baudW, y + app.S(122)},
                          L"Baud"});
    field(IDC_PORT, lx + pad, y + app.S(126), portW, app.S(34));
    RECT pf = app.fields[IDC_PORT];
    place(IDC_PORT_MENU, pf.right - app.S(36), pf.top + app.S(3), app.S(33), fh - app.S(6));
    field(IDC_BAUD, pf.right + gap, pf.top, baudW);
    place(IDC_TEST, pf.right + gap + baudW + gap, pf.top, testW, fh);

    place(IDC_KEYFALLBACK, rx + pad, y + app.S(38), inner, app.S(30));
    app.labels.push_back({{rx + pad + app.S(2), y + app.S(78), rx + half - pad, y + app.S(96)},
                          L"F13–F24 keys (for mice with extra buttons)"});
    place(IDC_EXTRA, rx + pad, y + app.S(100), inner, fh);
    place(IDC_AUTOSTART, rx + pad, y + app.S(148), inner, app.S(30));
    // randomize movement: label, then X and Y fields
    const int jy = y + app.S(192), jw = app.S(64), jl = app.S(18);
    const int jx = rx + half - pad - 2 * jw - 2 * jl - gap;
    app.labels.push_back({{rx + pad + app.S(2), jy, jx - gap, jy + fh}, L"Randomize movement (\u00B1 px)"});
    app.labels.push_back({{jx, jy, jx + jl, jy + fh}, L"X"});
    field(IDC_JITTER_X, jx + jl, jy, jw);
    app.labels.push_back({{jx + jl + jw + gap, jy, jx + 2 * jl + jw + gap, jy + fh}, L"Y"});
    field(IDC_JITTER_Y, jx + 2 * jl + jw + gap, jy, jw);

    // log card
    y += cardH + app.S(12);
    app.cards.push_back({{m, y, W - m, H - m}, L"LOG"});
    place(IDC_CLEAR, W - m - pad - app.S(72), y + app.S(8), app.S(72), app.S(28));
    app.fields[IDC_LOG] = {m + pad, y + app.S(42), W - m - pad, H - m - pad};
    place(IDC_LOG, m + pad + app.S(10), y + app.S(50), W - 2 * m - 2 * pad - app.S(14), H - m - pad - y - app.S(56));

    InvalidateRect(app.wnd, nullptr, FALSE);
}

// ------------------------------------------------------------------ window setup

LRESULT CALLBACK buttonHover(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_MOUSEMOVE && app.hover != h) {
        HWND old = app.hover;
        app.hover = h;
        if (old) InvalidateRect(old, nullptr, FALSE);
        InvalidateRect(h, nullptr, FALSE);
        TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, h, 0};
        TrackMouseEvent(&tme);
    } else if (msg == WM_MOUSELEAVE && app.hover == h) {
        app.hover = nullptr;
        InvalidateRect(h, nullptr, FALSE);
    }
    return DefSubclassProc(h, msg, wp, lp);
}

HWND make(const wchar_t* cls, const wchar_t* label, DWORD style, int id, HFONT font = nullptr) {
    HWND h = CreateWindowExW(0, cls, label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, 0, 0, 10, 10, app.wnd,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : app.font), TRUE);
    return h;
}

void button(int id, const wchar_t* label, Role role) {
    HWND h = make(L"BUTTON", label, BS_OWNERDRAW, id);
    app.roles[id] = role;
    SetWindowSubclass(h, buttonHover, 0, 0);
}

HFONT makeFont(int px, int weight, const wchar_t* face) {
    return CreateFontW(-app.S(px), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

int CALLBACK fontFound(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found) {
    *reinterpret_cast<bool*>(found) = true;
    return 0;
}

bool fontExists(const wchar_t* face) {
    LOGFONTW lf = {};
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
    bool found = false;
    HDC dc = GetDC(nullptr);
    EnumFontFamiliesExW(dc, &lf, fontFound, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

void enableDarkChrome() {
    // dark title bar (Windows 10 1809+ / 11) and caption colour on Windows 11
    BOOL on = TRUE;
    if (FAILED(DwmSetWindowAttribute(app.wnd, 20, &on, sizeof(on)))) DwmSetWindowAttribute(app.wnd, 19, &on, sizeof(on));
    COLORREF caption = Color::bg;
    DwmSetWindowAttribute(app.wnd, 35, &caption, sizeof(caption));
    // dark scrollbars and popup menus where Windows supports it
    SetWindowTheme(app.item(IDC_LOG), L"DarkMode_Explorer", nullptr);
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW v = {sizeof(v)};
    if (auto rtl = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")))
        rtl(&v);
    if (v.dwMajorVersion >= 10 && v.dwBuildNumber >= 18362) {
        if (HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
            using SetPreferredAppMode = int(WINAPI*)(int);
            if (auto fn = reinterpret_cast<SetPreferredAppMode>(GetProcAddress(ux, MAKEINTRESOURCEA(135)))) fn(2);
        }
    }
}

void createControls() {
    app.font = makeFont(14, FW_NORMAL, L"Segoe UI");
    app.fontBold = makeFont(14, FW_SEMIBOLD, L"Segoe UI");
    app.fontSmall = makeFont(12, FW_SEMIBOLD, L"Segoe UI");
    app.fontTitle = makeFont(22, FW_SEMIBOLD, L"Segoe UI");
    app.fontMono = makeFont(13, FW_NORMAL, L"Cascadia Mono");
    if (!fontExists(L"Cascadia Mono")) {
        DeleteObject(app.fontMono);
        app.fontMono = makeFont(13, FW_NORMAL, L"Consolas");
    }
    app.fieldBrush = CreateSolidBrush(Color::field);
    app.logBrush = CreateSolidBrush(Color::logBg);

    button(IDC_STOP, L"Stop", Role::Danger);
    button(IDC_START, L"Start", Role::Primary);
    button(IDC_OPEN, L"Open script", Role::Secondary);
    button(IDC_LOADCFG, L"Load config", Role::Secondary);
    button(IDC_SAVECFG, L"Save config", Role::Secondary);
    button(IDC_SAVEAS, L"Save as…", Role::Secondary);
    button(IDC_EDIT, L"Edit script", Role::Secondary);
    button(IDC_RELOAD, L"Reload", Role::Secondary);
    make(L"EDIT", L"", ES_AUTOHSCROLL, IDC_SCRIPT);
    button(IDC_SCRIPT_MENU, L"", Role::Chevron);
    button(IDC_BROWSE, L"Browse…", Role::Secondary);
    button(IDC_DEVICE, L"", Role::Dropdown);
    make(L"EDIT", L"", ES_AUTOHSCROLL, IDC_PORT);
    button(IDC_PORT_MENU, L"", Role::Chevron);
    make(L"EDIT", L"", ES_NUMBER | ES_AUTOHSCROLL, IDC_BAUD);
    button(IDC_TEST, L"Test", Role::Secondary);
    button(IDC_KEYFALLBACK, L"Type keys in software when the device can't", Role::Toggle);
    button(IDC_EXTRA, L"", Role::Dropdown);
    button(IDC_AUTOSTART, L"Start the script when the app opens", Role::Toggle);
    make(L"EDIT", L"0", ES_NUMBER | ES_AUTOHSCROLL, IDC_JITTER_X);
    make(L"EDIT", L"0", ES_NUMBER | ES_AUTOHSCROLL, IDC_JITTER_Y);
    button(IDC_CLEAR, L"Clear", Role::Secondary);
    HWND log = make(L"EDIT", L"", WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, IDC_LOG, app.fontMono);
    SendMessageW(log, EM_SETLIMITTEXT, 0, 0);

    const int m = app.S(4);
    for (int id : {IDC_SCRIPT, IDC_PORT, IDC_BAUD, IDC_JITTER_X, IDC_JITTER_Y}) SendMessageW(app.item(id), EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));
    SendMessageW(log, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(m, m));
    SendMessageW(app.item(IDC_SCRIPT), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Pick a .lua script"));
    SendMessageW(app.item(IDC_PORT), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"e.g. COM5"));

    ACCEL keys[] = {
        {FCONTROL | FVIRTKEY, 'O', IDC_OPEN},       {FCONTROL | FVIRTKEY, 'L', IDC_LOADCFG},
        {FCONTROL | FVIRTKEY, 'S', IDC_SAVECFG},    {FCONTROL | FSHIFT | FVIRTKEY, 'S', IDC_SAVEAS},
        {FVIRTKEY, VK_F5, IDC_START},               {FSHIFT | FVIRTKEY, VK_F5, IDC_STOP},
        {FCONTROL | FVIRTKEY, 'R', IDC_RELOAD},     {FCONTROL | FVIRTKEY, 'E', IDC_EDIT},
    };
    app.accel = CreateAcceleratorTableW(keys, sizeof(keys) / sizeof(keys[0]));
}

void onCommand(int id, int code) {
    // accelerators arrive for disabled buttons too
    if (code == 1 && !IsWindowEnabled(app.item(id))) return;
    switch (id) {
        case IDC_START: startScript(); break;
        case IDC_STOP: stopScript(); break;
        case IDC_RELOAD:
            if (app.running()) {
                stopScript();
                startScript();
            }
            break;
        case IDC_OPEN:
        case IDC_BROWSE: openScript(); break;
        case IDC_LOADCFG: {
            const std::string path = fileDialog(false, L"Bridge config (*.ini)\0*.ini\0All files (*.*)\0*.*\0",
                                                app.configPath.empty() ? ensureDir("configs") : dirName(app.configPath),
                                                "", L"ini");
            if (!path.empty()) loadConfigFile(path);
            break;
        }
        case IDC_SAVECFG: saveConfigFile(); break;
        case IDC_SAVEAS: saveConfigAs(); break;
        case IDC_EDIT: editScript(); break;
        case IDC_SCRIPT_MENU: scriptMenu(); break;
        case IDC_PORT_MENU: portMenu(); break;
        case IDC_DEVICE: deviceMenu(); break;
        case IDC_EXTRA: extraMenu(); break;
        case IDC_TEST: testDevice(); break;
        case IDC_CLEAR: clearLog(); break;
        case IDC_KEYFALLBACK:
            app.keyFallback = !app.keyFallback;
            InvalidateRect(app.item(id), nullptr, FALSE);
            refreshChrome();
            break;
        case IDC_AUTOSTART:
            app.autoStart = !app.autoStart;
            InvalidateRect(app.item(id), nullptr, FALSE);
            refreshChrome();
            break;
        case IDC_SCRIPT:
        case IDC_PORT:
        case IDC_BAUD:
        case IDC_JITTER_X:
        case IDC_JITTER_Y:
            if (code == EN_CHANGE) refreshChrome();
            if (code == EN_SETFOCUS || code == EN_KILLFOCUS) InvalidateRect(app.wnd, &app.fields[id], FALSE);
            break;
    }
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            paintWindow(dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SIZE:
            layout();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize = {app.S(780), app.S(690)};
            return 0;
        }
        case WM_DRAWITEM:
            drawButton(reinterpret_cast<const DRAWITEMSTRUCT*>(lp));
            return TRUE;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            const bool isLog = reinterpret_cast<HWND>(lp) == app.item(IDC_LOG);
            SetTextColor(dc, isLog ? RGB(0xc9, 0xd1, 0xd9) : IsWindowEnabled(reinterpret_cast<HWND>(lp)) ? Color::text : Color::muted);
            SetBkColor(dc, isLog ? Color::logBg : Color::field);
            return reinterpret_cast<LRESULT>(isLog ? app.logBrush : app.fieldBrush);
        }
        case WM_COMMAND:
            onCommand(LOWORD(wp), HIWORD(wp));
            return 0;
        case WM_DROPFILES: {
            wchar_t file[MAX_PATH];
            if (DragQueryFileW(reinterpret_cast<HDROP>(wp), 0, file, MAX_PATH) && !app.running()) {
                const std::string path = narrow(file);
                if (path.size() > 4 && _stricmp(path.c_str() + path.size() - 4, ".ini") == 0) loadConfigFile(path);
                else SetWindowTextW(app.item(IDC_SCRIPT), file);
            }
            DragFinish(reinterpret_cast<HDROP>(wp));
            return 0;
        }
        case WM_TIMER:
            flushLog();
            return 0;
        case WM_APP_CLEARLOG:
            clearLog();
            return 0;
        case WM_CLOSE:
            stopScript();
            saveState();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void initPaths() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    const std::string exe = narrow(path);
    app.exeDir = dirName(exe);
    std::string base = fileName(exe);
    const size_t dot = base.find_last_of('.');
    app.statePath = joinPath(app.exeDir, (dot == std::string::npos ? base : base.substr(0, dot)) + ".ini");
}

void loadState(const std::wstring& arg) {
    Config c;
    ConfigExtras extras;
    std::string err;
    loadConfig(app.statePath, c, err, &extras);
    app.configPath = extras["config_file"].empty() ? "" : normalizePath(joinPath(app.exeDir, extras["config_file"]));
    if (!app.configPath.empty() && !loadConfig(app.configPath, app.savedConfig, err)) app.configPath.clear();
    applyConfig(c);
    if (!arg.empty()) {
        const std::string a = narrow(arg);
        if (a.size() > 4 && _stricmp(a.c_str() + a.size() - 4, ".ini") == 0) loadConfigFile(a);
        else SetWindowTextW(app.item(IDC_SCRIPT), arg.c_str());
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    Gdiplus::GdiplusStartupInput gdiInput;
    ULONG_PTR gdiToken = 0;
    Gdiplus::GdiplusStartup(&gdiToken, &gdiInput, nullptr);
    timeBeginPeriod(1);  // 1 ms Sleep() resolution instead of ~15 ms
    initPaths();

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const std::wstring arg = argc > 1 ? argv[1] : L"";
    LocalFree(argv);

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"LogitechScriptBridge";
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    HDC screen = GetDC(nullptr);
    app.dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(nullptr, screen);

    app.wnd = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"Logitech Script Bridge",
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, app.S(860),
                              app.S(800), nullptr, nullptr, inst, nullptr);
    createControls();
    enableDarkChrome();
    loadState(arg);
    layout();
    refreshChrome();
    DragAcceptFiles(app.wnd, TRUE);
    SetTimer(app.wnd, kLogTimer, 50, nullptr);
    app.log("Pick a Logitech Lua script, choose your device and press Start (F5).\n"
            "Ctrl+S saves the settings as a config, Ctrl+L loads one. Drop a .lua or .ini file on the window.\n");
    ShowWindow(app.wnd, show);
    if (app.autoStart && !getText(IDC_SCRIPT).empty()) PostMessageW(app.wnd, WM_COMMAND, IDC_START, 0);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (TranslateAcceleratorW(app.wnd, app.accel, &msg)) continue;
        if (!IsDialogMessageW(app.wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    timeEndPeriod(1);
    Gdiplus::GdiplusShutdown(gdiToken);
    return 0;
}
