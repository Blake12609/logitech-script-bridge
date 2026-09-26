// A deliberately small Win32 window: pick a script, pick a device, press Start.
// Settings are kept in LogitechScriptBridge.ini next to the exe (portable).
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <shellapi.h>

#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "backend.h"
#include "engine.h"
#include "platform_win.h"

namespace {

enum {
    IDC_SCRIPT = 100, IDC_BROWSE, IDC_DEVICE, IDC_PORT, IDC_REFRESH, IDC_BAUD, IDC_KEYFALLBACK,
    IDC_START, IDC_STOP, IDC_CLEAR, IDC_STATUS, IDC_LOG,
    IDC_LBL_SCRIPT, IDC_LBL_DEVICE, IDC_LBL_PORT, IDC_LBL_BAUD,
};
const UINT WM_APP_CLEARLOG = WM_APP + 1;
const UINT_PTR kLogTimer = 1;

// ------------------------------------------------------------------ utf8 <-> utf16

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

// ------------------------------------------------------------------ app state

struct App {
    HWND wnd = nullptr;
    HFONT font = nullptr, mono = nullptr;
    int dpi = 96;
    std::wstring ini;

    WinInput input;
    std::unique_ptr<Backend> backend, fallback;
    std::unique_ptr<Engine> engine;

    std::mutex logMu;
    std::string pendingLog;

    HWND item(int id) const { return GetDlgItem(wnd, id); }
    int S(int v) const { return MulDiv(v, dpi, 96); }
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

void dryRunLog(void*, const std::string& s) { app.log(s); }

// ------------------------------------------------------------------ settings (ini next to exe)

void initIniPath() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p = path;
    size_t dot = p.find_last_of(L'.');
    app.ini = (dot == std::wstring::npos ? p : p.substr(0, dot)) + L".ini";
}

std::wstring iniGet(const wchar_t* key, const wchar_t* def) {
    wchar_t buf[1024];
    GetPrivateProfileStringW(L"settings", key, def, buf, 1024, app.ini.c_str());
    return buf;
}

void iniSet(const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(L"settings", key, value.c_str(), app.ini.c_str());
}

void saveSettings() {
    iniSet(L"script", getText(IDC_SCRIPT));
    int dev = static_cast<int>(SendMessageW(app.item(IDC_DEVICE), CB_GETCURSEL, 0, 0));
    iniSet(L"device", widen(kDevices[dev < 0 ? 0 : dev].key));
    iniSet(L"port", getText(IDC_PORT));
    iniSet(L"baud", getText(IDC_BAUD));
    iniSet(L"key_fallback", SendMessageW(app.item(IDC_KEYFALLBACK), BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0");
}

// ------------------------------------------------------------------ log view

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

// ------------------------------------------------------------------ actions

void refreshPorts() {
    HWND box = app.item(IDC_PORT);
    std::wstring current = getText(IDC_PORT);
    SendMessageW(box, CB_RESETCONTENT, 0, 0);
    for (const auto& p : listSerialPorts())
        SendMessageW(box, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(widen(p).c_str()));
    if (!current.empty()) SetWindowTextW(box, current.c_str());
    else SendMessageW(box, CB_SETCURSEL, 0, 0);
}

void setRunning(bool running) {
    EnableWindow(app.item(IDC_START), !running);
    EnableWindow(app.item(IDC_STOP), running);
    for (int id : {IDC_SCRIPT, IDC_BROWSE, IDC_DEVICE, IDC_PORT, IDC_REFRESH, IDC_BAUD, IDC_KEYFALLBACK})
        EnableWindow(app.item(id), !running);
    SetWindowTextW(app.item(IDC_STATUS), running ? L"Running" : L"Stopped");
}

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
    setRunning(false);
}

void error(const std::wstring& title, const std::string& msg) {
    MessageBoxW(app.wnd, widen(msg).c_str(), title.c_str(), MB_ICONERROR | MB_OK);
}

bool readFile(const std::wstring& path, std::string& out) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    if (out.compare(0, 3, "\xEF\xBB\xBF") == 0) out.erase(0, 3);  // UTF-8 BOM
    return true;
}

void startScript() {
    saveSettings();
    const std::wstring path = getText(IDC_SCRIPT);
    std::string source;
    if (path.empty() || !readFile(path, source)) {
        error(L"Script", "Cannot open the script file. Pick a .lua file first.");
        return;
    }
    int dev = static_cast<int>(SendMessageW(app.item(IDC_DEVICE), CB_GETCURSEL, 0, 0));
    const DeviceInfo& info = kDevices[dev < 0 ? 0 : dev];
    int baud = _wtoi(getText(IDC_BAUD).c_str());
    if (baud <= 0) baud = 115200;

    app.backend = createBackend(info.id, narrow(getText(IDC_PORT)), baud);
    if (auto* dry = dynamic_cast<DryRunBackend*>(app.backend.get())) dry->setLogger(dryRunLog, nullptr);
    std::string err;
    if (!app.backend->open(err)) {
        closeDevices();
        error(L"Device", err);
        return;
    }
    if (!app.backend->supportsKeyboard() &&
        SendMessageW(app.item(IDC_KEYFALLBACK), BM_GETCHECK, 0, 0) == BST_CHECKED)
        app.fallback = createSoftwareBackend();

    app.engine = std::make_unique<Engine>(*app.backend, app.input, [](const std::string& s) { app.log(s); },
                                          app.fallback.get());
    app.engine->onClearLog = [] { PostMessageW(app.wnd, WM_APP_CLEARLOG, 0, 0); };

    std::wstring name = path.substr(path.find_last_of(L"\\/") + 1);
    app.log(std::string("Device: ") + info.label + "\n");
    if (!app.engine->start(source, narrow(name), err)) {
        app.engine.reset();
        closeDevices();
        app.log("Could not load script:\n" + err + "\n");
        flushLog();
        return;
    }
    if (!app.input.startHook(app.engine.get(), err)) {
        stopScript();
        error(L"Mouse hook", err);
        return;
    }
    setRunning(true);
}

void browse() {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app.wnd;
    ofn.lpstrFilter = L"Lua scripts (*.lua)\0*.lua\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) SetWindowTextW(app.item(IDC_SCRIPT), file);
}

// ------------------------------------------------------------------ window

HWND make(const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD exStyle = 0) {
    HWND h = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, app.wnd,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(app.font), TRUE);
    return h;
}

void layout() {
    RECT rc;
    GetClientRect(app.wnd, &rc);
    const int W = rc.right, H = rc.bottom, h = app.S(23), m = app.S(10), lx = app.S(85);
    auto place = [&](int id, int x, int y, int w, int hh) { MoveWindow(app.item(id), x, y, w, hh, TRUE); };
    auto labelY = [&](int y) { return y + app.S(4); };

    int y = m;
    place(IDC_LBL_SCRIPT, m, labelY(y), lx - m, h);
    place(IDC_SCRIPT, lx, y, W - lx - m - app.S(90), h);
    place(IDC_BROWSE, W - m - app.S(85), y, app.S(85), h);
    y += app.S(31);
    place(IDC_LBL_DEVICE, m, labelY(y), lx - m, h);
    place(IDC_DEVICE, lx, y, app.S(230), app.S(200));
    y += app.S(31);
    place(IDC_LBL_PORT, m, labelY(y), lx - m, h);
    place(IDC_PORT, lx, y, app.S(140), app.S(200));
    place(IDC_REFRESH, lx + app.S(145), y, app.S(75), h);
    place(IDC_LBL_BAUD, lx + app.S(235), labelY(y), app.S(40), h);
    place(IDC_BAUD, lx + app.S(275), y, app.S(90), h);
    y += app.S(31);
    place(IDC_KEYFALLBACK, lx, y, W - lx - m, h);
    y += app.S(31);
    place(IDC_START, m, y, app.S(80), h + app.S(2));
    place(IDC_STOP, m + app.S(85), y, app.S(80), h + app.S(2));
    place(IDC_CLEAR, m + app.S(170), y, app.S(80), h + app.S(2));
    place(IDC_STATUS, m + app.S(262), labelY(y), app.S(150), h);
    y += app.S(35);
    place(IDC_LOG, m, y, W - 2 * m, H - y - m);
}

void createControls(const wchar_t* initialScript) {
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    app.font = CreateFontIndirectW(&ncm.lfMessageFont);
    app.mono = CreateFontW(-MulDiv(9, app.dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                           CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");

    make(L"STATIC", L"Lua script", 0, IDC_LBL_SCRIPT);
    make(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, IDC_SCRIPT, WS_EX_CLIENTEDGE);
    make(L"BUTTON", L"Browse...", WS_TABSTOP, IDC_BROWSE);
    make(L"STATIC", L"Device", 0, IDC_LBL_DEVICE);
    HWND dev = make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, IDC_DEVICE);
    make(L"STATIC", L"Port", 0, IDC_LBL_PORT);
    make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWN | CBS_AUTOHSCROLL, IDC_PORT);
    make(L"BUTTON", L"Refresh", WS_TABSTOP, IDC_REFRESH);
    make(L"STATIC", L"Baud", 0, IDC_LBL_BAUD);
    make(L"EDIT", L"", WS_TABSTOP | ES_NUMBER, IDC_BAUD, WS_EX_CLIENTEDGE);
    make(L"BUTTON", L"Type keys in software when the device can't (MAKCU)", WS_TABSTOP | BS_AUTOCHECKBOX,
         IDC_KEYFALLBACK);
    make(L"BUTTON", L"Start", WS_TABSTOP | BS_DEFPUSHBUTTON, IDC_START);
    make(L"BUTTON", L"Stop", WS_TABSTOP, IDC_STOP);
    make(L"BUTTON", L"Clear log", WS_TABSTOP, IDC_CLEAR);
    make(L"STATIC", L"Stopped", 0, IDC_STATUS);
    HWND log = make(L"EDIT", L"", WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, IDC_LOG,
                    WS_EX_CLIENTEDGE);
    SendMessageW(log, WM_SETFONT, reinterpret_cast<WPARAM>(app.mono), TRUE);
    SendMessageW(log, EM_SETLIMITTEXT, 0, 0);

    for (const auto& d : kDevices) SendMessageW(dev, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(widen(d.label).c_str()));
    const std::string savedDev = narrow(iniGet(L"device", L"makcu"));
    int sel = 0;
    for (int i = 0; i < 4; i++)
        if (savedDev == kDevices[i].key) sel = i;
    SendMessageW(dev, CB_SETCURSEL, sel, 0);

    SetWindowTextW(app.item(IDC_SCRIPT), initialScript && *initialScript ? initialScript
                                                                          : iniGet(L"script", L"").c_str());
    SetWindowTextW(app.item(IDC_PORT), iniGet(L"port", L"").c_str());
    SetWindowTextW(app.item(IDC_BAUD), iniGet(L"baud", L"115200").c_str());
    SendMessageW(app.item(IDC_KEYFALLBACK), BM_SETCHECK,
                 iniGet(L"key_fallback", L"1") == L"1" ? BST_CHECKED : BST_UNCHECKED, 0);
    refreshPorts();
    setRunning(false);
    layout();
    app.log("Pick a Logitech Lua script (or drop one on this window), choose your device and press Start.\n");
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            layout();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize = {app.S(560), app.S(360)};
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_BROWSE: browse(); break;
                case IDC_REFRESH: refreshPorts(); break;
                case IDC_START: startScript(); break;
                case IDC_STOP: stopScript(); break;
                case IDC_CLEAR: clearLog(); break;
            }
            return 0;
        case WM_DROPFILES: {
            wchar_t file[MAX_PATH];
            if (DragQueryFileW(reinterpret_cast<HDROP>(wp), 0, file, MAX_PATH) && !app.engine)
                SetWindowTextW(app.item(IDC_SCRIPT), file);
            DragFinish(reinterpret_cast<HDROP>(wp));
            return 0;
        }
        case WM_TIMER:
            flushLog();
            return 0;
        case WM_APP_CLEARLOG:
            clearLog();
            return 0;
        case WM_CTLCOLORSTATIC:
            // keep the read-only log white like a normal text box
            if (reinterpret_cast<HWND>(lp) == app.item(IDC_LOG)) {
                SetBkColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_WINDOW));
                return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
            }
            break;
        case WM_CLOSE:
            stopScript();
            saveSettings();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    timeBeginPeriod(1);  // 1 ms Sleep() resolution instead of ~15 ms
    initIniPath();

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring initialScript = argc > 1 ? argv[1] : L"";
    LocalFree(argv);

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"LogitechScriptBridge";
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    HDC screen = GetDC(nullptr);
    app.dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(nullptr, screen);

    app.wnd = CreateWindowExW(0, wc.lpszClassName, L"Logitech Script Bridge", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                              CW_USEDEFAULT, app.S(640), app.S(460), nullptr, nullptr, inst, nullptr);
    createControls(initialScript.c_str());
    DragAcceptFiles(app.wnd, TRUE);
    SetTimer(app.wnd, kLogTimer, 50, nullptr);
    ShowWindow(app.wnd, show);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(app.wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    timeEndPeriod(1);
    return 0;
}
