// The window: a custom-drawn dark UI on plain Win32 + GDI+ (no UI framework).
//
// Everything visible is painted by this file: a static "backdrop" (background,
// cards, fields) is rendered once into a cached bitmap, owner-drawn controls copy
// the piece of backdrop behind them and draw on top, and the window paints text,
// the live mouse picture and animations over the backdrop.
//
// App state lives in LogitechScriptBridge.ini next to the exe (portable); named
// configs can be saved/loaded anywhere (default: configs\ next to the exe).
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <richedit.h>
#include <ole2.h>
#include <tom.h>
#include <shellapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <objidl.h>  // GDI+ needs COM declarations that WIN32_LEAN_AND_MEAN leaves out
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "backend.h"
#include "config.h"
#include "engine.h"
#include "platform_win.h"

namespace {

const wchar_t* const kVersion = L"v1.5";

// ------------------------------------------------------------------ theme

namespace Color {
const COLORREF bgTop = RGB(0x12, 0x14, 0x1a);
const COLORREF bgBottom = RGB(0x0c, 0x0d, 0x11);
const COLORREF card = RGB(0x17, 0x1a, 0x21);
const COLORREF cardBorder = RGB(0x24, 0x29, 0x33);
const COLORREF field = RGB(0x1f, 0x23, 0x2c);
const COLORREF fieldHover = RGB(0x27, 0x2c, 0x37);
const COLORREF border = RGB(0x2e, 0x34, 0x40);
const COLORREF borderHover = RGB(0x40, 0x48, 0x57);
const COLORREF text = RGB(0xe9, 0xec, 0xf1);
const COLORREF muted = RGB(0x8a, 0x93, 0xa3);
const COLORREF faint = RGB(0x5a, 0x63, 0x71);
const COLORREF accent = RGB(0x5b, 0x8c, 0xff);
const COLORREF accent2 = RGB(0x8b, 0x5c, 0xf6);
const COLORREF accentHover = RGB(0x7c, 0xa3, 0xff);
const COLORREF success = RGB(0x34, 0xd3, 0x99);
const COLORREF danger = RGB(0xf2, 0x5f, 0x5c);
const COLORREF logBg = RGB(0x0f, 0x11, 0x15);
const COLORREF white = RGB(0xff, 0xff, 0xff);
const COLORREF black = RGB(0x00, 0x00, 0x00);
}  // namespace Color

COLORREF mix(COLORREF a, COLORREF b, double t) {
    auto ch = [&](int shift) {
        const int x = (a >> shift) & 0xFF, y = (b >> shift) & 0xFF;
        return static_cast<COLORREF>(x + (y - x) * t) << shift;
    };
    return ch(0) | ch(8) | ch(16);
}

Gdiplus::Color gp(COLORREF c, BYTE alpha = 255) { return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c)); }

// GDI+ for shapes, GDI for crisp ClearType text. Everything is given in window
// coordinates; ox/oy shift them so the same code can draw into a control's bitmap.
struct Canvas {
    HDC dc;
    Gdiplus::Graphics g;
    int ox, oy;
    explicit Canvas(HDC d, int x = 0, int y = 0) : dc(d), g(d), ox(x), oy(y) {
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    }
    Gdiplus::RectF rf(const RECT& r, float inset = 0) const {
        return Gdiplus::RectF(r.left - ox + inset, r.top - oy + inset, r.right - r.left - 2 * inset,
                              r.bottom - r.top - 2 * inset);
    }
};

void roundPath(Gdiplus::GraphicsPath& p, Gdiplus::RectF r, float rad) {
    const float d = std::min(rad * 2, std::min(r.Width, r.Height));
    if (d <= 0.5f) {
        p.AddRectangle(r);
        return;
    }
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
}

void fillRound(Canvas& c, const RECT& r, float rad, COLORREF col, BYTE alpha = 255) {
    Gdiplus::GraphicsPath p;
    roundPath(p, c.rf(r), rad);
    Gdiplus::SolidBrush b(gp(col, alpha));
    c.g.FillPath(&b, &p);
}

void strokeRound(Canvas& c, const RECT& r, float rad, COLORREF col, float w = 1, BYTE alpha = 255) {
    Gdiplus::GraphicsPath p;
    roundPath(p, c.rf(r, w / 2), rad);
    Gdiplus::Pen pen(gp(col, alpha), w);
    c.g.DrawPath(&pen, &p);
}

void gradientRound(Canvas& c, const RECT& r, float rad, COLORREF a, COLORREF b, float angle, BYTE alpha = 255) {
    Gdiplus::GraphicsPath p;
    roundPath(p, c.rf(r), rad);
    Gdiplus::RectF box = c.rf(r);
    box.Inflate(1, 1);
    Gdiplus::LinearGradientBrush br(box, gp(a, alpha), gp(b, alpha), angle);
    c.g.FillPath(&br, &p);
}

void softShadow(Canvas& c, const RECT& r, float rad, int size) {
    for (int i = size; i >= 1; i--) {
        RECT s = r;
        InflateRect(&s, i, i);
        OffsetRect(&s, 0, i / 2 + 1);
        fillRound(c, s, rad + i, Color::black, static_cast<BYTE>(26 / (1 + i)));
    }
}

void glow(Canvas& c, float cx, float cy, float radius, COLORREF col, BYTE alpha) {
    Gdiplus::GraphicsPath p;
    p.AddEllipse(cx - radius - c.ox, cy - radius - c.oy, radius * 2, radius * 2);
    Gdiplus::PathGradientBrush br(&p);
    br.SetCenterColor(gp(col, alpha));
    Gdiplus::Color edge = gp(col, 0);
    INT n = 1;
    br.SetSurroundColors(&edge, &n);
    c.g.FillPath(&br, &p);
}

void text(Canvas& c, const std::wstring& s, RECT r, HFONT font, COLORREF col, UINT flags = DT_LEFT | DT_VCENTER,
          int tracking = 0) {
    c.g.Flush(Gdiplus::FlushIntentionSync);
    OffsetRect(&r, -c.ox, -c.oy);
    SelectObject(c.dc, font);
    SetTextColor(c.dc, col);
    SetBkMode(c.dc, TRANSPARENT);
    SetTextCharacterExtra(c.dc, tracking);
    if (!(flags & DT_WORDBREAK)) flags |= DT_SINGLELINE | DT_END_ELLIPSIS;
    DrawTextW(c.dc, s.c_str(), -1, &r, flags | DT_NOPREFIX);
    SetTextCharacterExtra(c.dc, 0);
}

int textWidth(HDC dc, const std::wstring& s, HFONT font, int tracking = 0) {
    SelectObject(dc, font);
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.c_str(), static_cast<int>(s.size()), &sz);
    return sz.cx + tracking * static_cast<int>(s.size());
}

// ------------------------------------------------------------------ icons

enum class Icon {
    None, Play, Stop, Folder, Save, Upload, Edit, Reload, List, Chevron, Check, Close, Minimize, Maximize,
    Restore, Trash, Pointer, Info, Plus,
};

void drawIcon(Canvas& c, Icon ic, const RECT& box, COLORREF col, float stroke) {
    const float s = static_cast<float>(std::min(box.right - box.left, box.bottom - box.top));
    const float x0 = box.left - c.ox + (box.right - box.left - s) / 2, y0 = box.top - c.oy + (box.bottom - box.top - s) / 2;
    auto P = [&](float fx, float fy) { return Gdiplus::PointF(x0 + fx * s, y0 + fy * s); };
    Gdiplus::Pen pen(gp(col), stroke);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::SolidBrush brush(gp(col));
    auto lines = [&](std::vector<Gdiplus::PointF> v, bool closed = false) {
        if (closed) c.g.DrawPolygon(&pen, v.data(), static_cast<INT>(v.size()));
        else c.g.DrawLines(&pen, v.data(), static_cast<INT>(v.size()));
    };
    auto rect = [&](float l, float t, float r, float b, float rad) {
        Gdiplus::GraphicsPath p;
        roundPath(p, Gdiplus::RectF(x0 + l * s, y0 + t * s, (r - l) * s, (b - t) * s), rad * s);
        c.g.DrawPath(&pen, &p);
    };
    switch (ic) {
        case Icon::Play: {
            Gdiplus::PointF v[] = {P(0.32f, 0.2f), P(0.8f, 0.5f), P(0.32f, 0.8f)};
            c.g.FillPolygon(&brush, v, 3);
            c.g.DrawPolygon(&pen, v, 3);
            break;
        }
        case Icon::Stop: {
            Gdiplus::GraphicsPath p;
            roundPath(p, Gdiplus::RectF(x0 + 0.26f * s, y0 + 0.26f * s, 0.48f * s, 0.48f * s), 0.08f * s);
            c.g.FillPath(&brush, &p);
            break;
        }
        case Icon::Folder:
            lines({P(0.12f, 0.26f), P(0.4f, 0.26f), P(0.48f, 0.35f), P(0.88f, 0.35f), P(0.88f, 0.78f), P(0.12f, 0.78f)}, true);
            break;
        case Icon::Save:
            rect(0.16f, 0.16f, 0.84f, 0.84f, 0.1f);
            lines({P(0.32f, 0.16f), P(0.32f, 0.36f), P(0.62f, 0.36f), P(0.62f, 0.16f)});
            rect(0.3f, 0.56f, 0.7f, 0.84f, 0.02f);
            break;
        case Icon::Upload:
            lines({P(0.16f, 0.6f), P(0.16f, 0.84f), P(0.84f, 0.84f), P(0.84f, 0.6f)});
            lines({P(0.5f, 0.66f), P(0.5f, 0.16f)});
            lines({P(0.32f, 0.34f), P(0.5f, 0.16f), P(0.68f, 0.34f)});
            break;
        case Icon::Edit:
            lines({P(0.64f, 0.18f), P(0.82f, 0.36f), P(0.38f, 0.8f), P(0.18f, 0.82f), P(0.2f, 0.62f)}, true);
            lines({P(0.54f, 0.28f), P(0.72f, 0.46f)});
            break;
        case Icon::Reload:
            c.g.DrawArc(&pen, x0 + 0.2f * s, y0 + 0.2f * s, 0.6f * s, 0.6f * s, -30, 290);
            lines({P(0.62f, 0.2f), P(0.78f, 0.35f), P(0.84f, 0.14f)});
            break;
        case Icon::List:
            for (float y : {0.3f, 0.5f, 0.7f}) {
                lines({P(0.38f, y), P(0.84f, y)});
                c.g.FillEllipse(&brush, x0 + 0.16f * s, y0 + (y - 0.05f) * s, 0.1f * s, 0.1f * s);
            }
            break;
        case Icon::Chevron:
            lines({P(0.3f, 0.4f), P(0.5f, 0.6f), P(0.7f, 0.4f)});
            break;
        case Icon::Check:
            lines({P(0.2f, 0.52f), P(0.42f, 0.74f), P(0.8f, 0.3f)});
            break;
        case Icon::Close:
            lines({P(0.3f, 0.3f), P(0.7f, 0.7f)});
            lines({P(0.7f, 0.3f), P(0.3f, 0.7f)});
            break;
        case Icon::Minimize:
            lines({P(0.3f, 0.5f), P(0.7f, 0.5f)});
            break;
        case Icon::Maximize:
            rect(0.3f, 0.3f, 0.7f, 0.7f, 0.04f);
            break;
        case Icon::Restore:
            rect(0.3f, 0.38f, 0.62f, 0.7f, 0.04f);
            lines({P(0.38f, 0.3f), P(0.7f, 0.3f), P(0.7f, 0.62f)});
            break;
        case Icon::Trash:
            lines({P(0.18f, 0.28f), P(0.82f, 0.28f)});
            lines({P(0.4f, 0.28f), P(0.42f, 0.16f), P(0.58f, 0.16f), P(0.6f, 0.28f)});
            lines({P(0.27f, 0.28f), P(0.32f, 0.84f), P(0.68f, 0.84f), P(0.73f, 0.28f)});
            break;
        case Icon::Pointer:
            lines({P(0.28f, 0.16f), P(0.28f, 0.78f), P(0.43f, 0.64f), P(0.54f, 0.86f), P(0.63f, 0.82f), P(0.52f, 0.6f),
                   P(0.72f, 0.6f)}, true);
            break;
        case Icon::Info:
            c.g.DrawEllipse(&pen, x0 + 0.12f * s, y0 + 0.12f * s, 0.76f * s, 0.76f * s);
            lines({P(0.5f, 0.46f), P(0.5f, 0.68f)});
            c.g.FillEllipse(&brush, x0 + 0.45f * s, y0 + 0.28f * s, 0.1f * s, 0.1f * s);
            break;
        case Icon::Plus:
            lines({P(0.5f, 0.2f), P(0.5f, 0.8f)});
            lines({P(0.2f, 0.5f), P(0.8f, 0.5f)});
            break;
        case Icon::None:
            break;
    }
}

// App logo: gradient tile with a mouse outline.
void drawLogo(Canvas& c, const RECT& box) {
    gradientRound(c, box, (box.right - box.left) * 0.28f, Color::accent, Color::accent2, 45);
    const float s = static_cast<float>(box.right - box.left);
    Gdiplus::Pen pen(gp(Color::white), std::max(1.2f, s / 14));
    const float x = box.left - c.ox + s * 0.33f, y = box.top - c.oy + s * 0.2f, w = s * 0.34f, h = s * 0.6f;
    Gdiplus::GraphicsPath p;
    roundPath(p, Gdiplus::RectF(x, y, w, h), w / 2);
    c.g.DrawPath(&pen, &p);
    c.g.DrawLine(&pen, x + w / 2, y + h * 0.12f, x + w / 2, y + h * 0.34f);
}

// ------------------------------------------------------------------ controls

enum Id {
    IDC_START = 100, IDC_OPEN, IDC_LIBRARY, IDC_EDIT, IDC_RELOAD, IDC_CONFIG, IDC_MIN, IDC_MAX, IDC_CLOSE,
    IDC_DEVICE, IDC_PORT, IDC_PORT_MENU, IDC_BAUD, IDC_TEST, IDC_KEYFALLBACK, IDC_AUTOSTART, IDC_EXTRA, IDC_HOTKEY,
    IDC_JITTER_MIN, IDC_JITTER_MAX, IDC_CLEAR, IDC_LOG, IDC_TAB_SCRIPT, IDC_TAB_LOG, IDC_EDITOR, IDC_NEW,
    IDC_REVERT, IDC_SAVESCRIPT, IDC_EXPAND,
    // commands without a control of their own (shortcuts and menus)
    IDC_STOP = 200, IDC_SAVECFG, IDC_SAVEAS, IDC_LOADCFG,
};

enum class Role { Hero, Ghost, Secondary, Dropdown, Chevron, Toggle, Caption, CaptionClose, Chip, Tab };

struct ButtonInfo {
    Role role;
    Icon icon;
};

const UINT WM_APP_CLEARLOG = WM_APP + 1;
const UINT_PTR kLogTimer = 1, kAnimTimer = 2, kEditTimer = 3;
const int kHotkeyId = 1;

const char* const kExtraKeys[] = {"off", "mouse", "gkeys"};
const wchar_t* const kExtraLabels[] = {L"Off", L"Mouse buttons 6–17", L"G-keys G1–G12"};

const struct { const char* key; const wchar_t* label; UINT vk; } kHotkeys[] = {
    {"off", L"Off", 0},          {"f6", L"F6", VK_F6},    {"f7", L"F7", VK_F7},     {"f8", L"F8", VK_F8},
    {"f9", L"F9", VK_F9},        {"f10", L"F10", VK_F10}, {"f11", L"F11", VK_F11}, {"f12", L"F12", VK_F12},
    {"pause", L"Pause", VK_PAUSE}, {"scrolllock", L"Scroll Lock", VK_SCROLL},
};
const int kHotkeyCount = sizeof(kHotkeys) / sizeof(kHotkeys[0]);

// A longer explanation per device, shown in the device card.
const wchar_t* const kDeviceHelp[kDeviceCount] = {
    L"Mouse only. Keys the script presses can be typed by Windows instead (see Options). Pick its COM port and press Test.",
    L"Mouse only. Keys the script presses can be typed by Windows instead (see Options). Pick its COM port and press Test.",
    L"Mouse and keyboard. Flash firmware/esp32s3_bridge, plug in the board's USB port, pick its COM port and press Test.",
    L"Mouse and keyboard. Flash firmware/arduino_hid_bridge, pick its COM port and press Test. Back/forward need an RP2040 board.",
    L"No hardware: Windows SendInput. Handy for trying scripts, but many games ignore injected input.",
    L"Nothing is sent. Every click, move and key the script makes is written to the log instead.",
};

enum class LogKind { Script, Info, Error, Dry };
COLORREF logColor(LogKind k) {
    switch (k) {
        case LogKind::Info: return RGB(0x86, 0x8f, 0x9e);
        case LogKind::Error: return RGB(0xff, 0x7b, 0x72);
        case LogKind::Dry: return RGB(0x7c, 0xa8, 0xff);
        default: return RGB(0xe6, 0xe9, 0xee);
    }
}

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

std::wstring formatPx(double v) {
    wchar_t buf[32];
    swprintf(buf, 32, L"%g", v);
    return buf;
}

struct Card {
    RECT rect;
    std::wstring caption;
};

struct Label {
    RECT rect;
    std::wstring text;
};

struct ToggleAnim {
    float from, to;
    DWORD start;
};

struct App {
    HWND wnd = nullptr;
    int dpi = 96;
    HFONT font = nullptr, fontBold = nullptr, fontSmall = nullptr, fontCaption = nullptr, fontTitle = nullptr,
          fontHero = nullptr, fontTiny = nullptr, fontMono = nullptr;
    HBRUSH fieldBrush = nullptr;
    std::wstring monoFace = L"Consolas";

    std::string exeDir, statePath, configPath, scriptPath;
    Config savedConfig;  // contents of configPath, to show "unsaved changes"
    int device = 0, extraKeys = 0, hotkey = 0;
    bool keyFallback = true, autoStart = false, hotkeyRegistered = false;
    enum class Status { Stopped, Running, Error } status = Status::Stopped;

    WinInput input;
    std::unique_ptr<Backend> backend, fallback;
    std::unique_ptr<Engine> engine;

    std::mutex logMu;
    std::vector<std::pair<LogKind, std::string>> pendingLog;

    std::map<int, ButtonInfo> buttons;
    std::map<int, RECT> fields;  // painted boxes around edit controls (by edit id)
    std::map<int, ToggleAnim> anims;
    std::vector<Card> cards;
    std::vector<Label> labels;
    int titleH = 44;
    RECT heroRect{}, ringRect{}, bigLabelRect{}, helpRect{}, mouseCard{}, mouseArea{}, legendRect{};
    HWND hover = nullptr, tooltip = nullptr;
    HACCEL accel = nullptr;
    unsigned lastButtons = ~0u;

    // script editor / log tabs
    int tab = 0;               // 0 = script editor, 1 = log
    bool expanded = false;     // device/options/mouse cards hidden to give the editor room
    bool untitled = false;     // a new script that hasn't been saved yet
    bool editorDirty = false, editorCrlf = true, loadingEditor = false, highlighting = false;
    bool logUnseen = false, logUnseenError = false;
    std::wstring editorSaved;  // text as last loaded/saved, to spot unsaved changes
    std::string syntaxError;
    int syntaxLine = 0;
    LONG gutterFirst = -1, gutterCaret = -1;
    ITextDocument* editorDoc = nullptr;
    RECT tabsRect{}, gutterRect{}, statusRect{};

    // cached backdrop
    HDC backdropDC = nullptr;
    HBITMAP backdropBmp = nullptr;
    SIZE backdropSize{};
    bool backdropDirty = true;

    HWND item(int id) const { return GetDlgItem(wnd, id); }
    int S(int v) const { return MulDiv(v, dpi, 96); }
    float Sf(float v) const { return v * dpi / 96.0f; }
    bool running() const { return engine != nullptr; }
    void log(const std::string& s, LogKind kind = LogKind::Info) {
        std::lock_guard<std::mutex> lock(logMu);
        if (!pendingLog.empty() && pendingLog.back().first == kind) pendingLog.back().second += s;
        else pendingLog.emplace_back(kind, s);
    }
} app;

std::wstring getText(int id) {
    HWND h = app.item(id);
    std::wstring s(GetWindowTextLengthW(h), L'\0');
    GetWindowTextW(h, &s[0], static_cast<int>(s.size()) + 1);
    return s;
}

RECT clientRectOf(int id) {
    RECT r{};
    if (!GetWindowRect(app.item(id), &r)) return r;
    MapWindowPoints(nullptr, app.wnd, reinterpret_cast<POINT*>(&r), 2);
    return r;
}

void redrawAll() {
    app.backdropDirty = true;
    RedrawWindow(app.wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

// ------------------------------------------------------------------ config <-> UI

void refreshChrome();

double readPx(int id) {
    const double v = _wtof(getText(id).c_str());
    return std::isfinite(v) && v > 0 ? std::min(v, 100.0) : 0.0;
}

Config currentConfig() {
    Config c;
    c.script = app.scriptPath;
    c.device = kDevices[app.device].key;
    c.port = narrow(getText(IDC_PORT));
    c.baud = getText(IDC_BAUD).empty() ? 115200 : std::max(1, _wtoi(getText(IDC_BAUD).c_str()));
    c.keyFallback = app.keyFallback;
    c.extraKeys = kExtraKeys[app.extraKeys];
    c.autoStart = app.autoStart;
    c.jitterMin = readPx(IDC_JITTER_MIN);
    c.jitterMax = readPx(IDC_JITTER_MAX);
    c.hotkey = kHotkeys[app.hotkey].key;
    return c;
}

void registerHotkey() {
    if (app.hotkeyRegistered) UnregisterHotKey(app.wnd, kHotkeyId);
    app.hotkeyRegistered = false;
    const UINT vk = kHotkeys[app.hotkey].vk;
    if (!vk) return;
    app.hotkeyRegistered = RegisterHotKey(app.wnd, kHotkeyId, MOD_NOREPEAT, vk) != 0;
    if (app.hotkeyRegistered)
        app.log("Press " + narrow(kHotkeys[app.hotkey].label) + " anywhere to start or stop the script.\n");
    else
        app.log(narrow(kHotkeys[app.hotkey].label) + " is already used by another program; pick another hotkey.\n",
                LogKind::Error);
}

bool confirmDiscardEdits();
void loadEditor(const std::string& path);

// Switches to another script, offering to save unsaved edits first.
void setScript(const std::string& path) {
    if (path == app.scriptPath && !app.untitled) return;
    if (!confirmDiscardEdits()) return;
    app.scriptPath = path;
    app.untitled = false;
    loadEditor(path);
    refreshChrome();
}

void applyConfig(const Config& c) {
    setScript(c.script);
    const DeviceInfo* d = findDevice(c.device);
    app.device = d ? static_cast<int>(d - kDevices) : 0;
    SetWindowTextW(app.item(IDC_PORT), widen(c.port).c_str());
    SetWindowTextW(app.item(IDC_BAUD), std::to_wstring(c.baud).c_str());
    app.keyFallback = c.keyFallback;
    app.autoStart = c.autoStart;
    SetWindowTextW(app.item(IDC_JITTER_MIN), formatPx(c.jitterMin).c_str());
    SetWindowTextW(app.item(IDC_JITTER_MAX), formatPx(c.jitterMax).c_str());
    app.extraKeys = 0;
    for (int i = 0; i < 3; i++)
        if (c.extraKeys == kExtraKeys[i]) app.extraKeys = i;
    app.hotkey = 0;
    for (int i = 0; i < kHotkeyCount; i++)
        if (c.hotkey == kHotkeys[i].key) app.hotkey = i;
    registerHotkey();
    for (int id : {IDC_DEVICE, IDC_KEYFALLBACK, IDC_AUTOSTART, IDC_EXTRA, IDC_HOTKEY})
        InvalidateRect(app.item(id), nullptr, FALSE);
    refreshChrome();
}

void saveState() {
    ConfigExtras extras;
    if (!app.configPath.empty()) extras["config_file"] = makeRelative(app.configPath, app.exeDir);
    WINDOWPLACEMENT wp = {sizeof(wp)};
    if (GetWindowPlacement(app.wnd, &wp)) {
        const RECT& r = wp.rcNormalPosition;
        extras["window"] = std::to_string(r.left) + "," + std::to_string(r.top) + "," + std::to_string(r.right - r.left) +
                           "," + std::to_string(r.bottom - r.top) + (wp.showCmd == SW_SHOWMAXIMIZED ? ",max" : "");
    }
    std::string err;
    saveConfig(app.statePath, currentConfig(), err, &extras);
}

bool modified() { return !app.configPath.empty() && currentConfig() != app.savedConfig; }

void setStatus(App::Status s) {
    if (app.status == s) return;
    app.status = s;
    redrawAll();  // hero border and glow follow the status
}

// Title, texts and enabled states follow the current values.
void refreshChrome() {
    std::wstring title = L"Logitech Script Bridge";
    if (!app.configPath.empty()) title += L" – " + widen(fileName(app.configPath)) + (modified() ? L"*" : L"");
    SetWindowTextW(app.wnd, title.c_str());

    const bool run = app.running(), serial = kDevices[app.device].serial;
    EnableWindow(app.item(IDC_RELOAD), run);
    for (int id : {IDC_OPEN, IDC_LIBRARY, IDC_DEVICE, IDC_EXTRA, IDC_AUTOSTART, IDC_JITTER_MIN, IDC_JITTER_MAX,
                   IDC_HOTKEY, IDC_TEST})
        EnableWindow(app.item(id), !run);
    for (int id : {IDC_PORT, IDC_PORT_MENU, IDC_BAUD}) EnableWindow(app.item(id), !run && serial);
    EnableWindow(app.item(IDC_KEYFALLBACK), !run && !kDevices[app.device].keyboard);
    EnableWindow(app.item(IDC_NEW), !run);
    InvalidateRect(app.wnd, &app.heroRect, FALSE);
    InvalidateRect(app.wnd, &app.helpRect, FALSE);
    for (int id : {IDC_CONFIG, IDC_START, IDC_DEVICE}) InvalidateRect(app.item(id), nullptr, FALSE);
}

// ------------------------------------------------------------------ log

void flushLog() {
    std::vector<std::pair<LogKind, std::string>> chunks;
    {
        std::lock_guard<std::mutex> lock(app.logMu);
        chunks.swap(app.pendingLog);
    }
    if (chunks.empty()) return;
    HWND box = app.item(IDC_LOG);
    if (GetWindowTextLengthW(box) > 400000) {  // keep the log from growing forever
        CHARRANGE head = {0, GetWindowTextLengthW(box) / 2};
        SendMessageW(box, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&head));
        SendMessageW(box, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
    }
    for (const auto& chunk : chunks) {
        std::string crlf;
        for (size_t i = 0; i < chunk.second.size(); i++) {
            if (chunk.second[i] == '\n' && (i == 0 || chunk.second[i - 1] != '\r')) crlf += '\r';
            crlf += chunk.second[i];
        }
        CHARRANGE end = {-1, -1};
        SendMessageW(box, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&end));
        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_COLOR;
        cf.crTextColor = logColor(chunk.first);
        SendMessageW(box, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
        SendMessageW(box, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(widen(crlf).c_str()));
    }
    SendMessageW(box, WM_VSCROLL, SB_BOTTOM, 0);
    if (app.tab != 1) {
        app.logUnseen = true;
        for (const auto& chunk : chunks)
            if (chunk.first == LogKind::Error) app.logUnseenError = true;
        InvalidateRect(app.item(IDC_TAB_LOG), nullptr, FALSE);
    }
}

void clearLog() {
    flushLog();
    SetWindowTextW(app.item(IDC_LOG), L"");
}

// Engine messages share the script's log callback; tell them apart by their wording.
LogKind engineLogKind(const std::string& s) {
    if (s.rfind("Script error", 0) == 0 || s.rfind("Unknown ", 0) == 0) return LogKind::Error;
    if (s.rfind("Script loaded", 0) == 0 || s.rfind("Script stopped", 0) == 0 || s.rfind("This device", 0) == 0 ||
        s.rfind("PlayMacro", 0) == 0)
        return LogKind::Info;
    return LogKind::Script;
}

void dryRunLog(void*, const std::string& s) { app.log(s, LogKind::Dry); }

// ------------------------------------------------------------------ script editor

// Lua syntax highlighting: split the text into coloured spans.
enum class Tok { Default, Keyword, Api, Builtin, String, Number, Comment };

COLORREF tokColor(Tok t) {
    switch (t) {
        case Tok::Keyword: return RGB(0xc7, 0x92, 0xea);
        case Tok::Api: return RGB(0x82, 0xaa, 0xff);
        case Tok::Builtin: return RGB(0x89, 0xdd, 0xff);
        case Tok::String: return RGB(0xc3, 0xe8, 0x8d);
        case Tok::Number: return RGB(0xf7, 0x8c, 0x6c);
        case Tok::Comment: return RGB(0x6b, 0x73, 0x85);
        default: return RGB(0xd6, 0xdb, 0xe4);
    }
}

Tok classifyWord(const std::wstring& w) {
    static const std::set<std::wstring> keywords = {
        L"and", L"break", L"do", L"else", L"elseif", L"end", L"false", L"for", L"function", L"goto", L"if", L"in",
        L"local", L"nil", L"not", L"or", L"repeat", L"return", L"then", L"true", L"until", L"while"};
    static const std::set<std::wstring> api = {
        L"OnEvent", L"GetMKeyState", L"SetMKeyState", L"Sleep", L"OutputLogMessage", L"GetRunningTime", L"GetDate",
        L"ClearLog", L"PressKey", L"ReleaseKey", L"PressAndReleaseKey", L"IsModifierPressed", L"PressMouseButton",
        L"ReleaseMouseButton", L"PressAndReleaseMouseButton", L"IsMouseButtonPressed", L"MoveMouseTo",
        L"MoveMouseWheel", L"MoveMouseRelative", L"MoveMouseToVirtual", L"GetMousePosition", L"OutputLCDMessage",
        L"ClearLCD", L"PlayMacro", L"PressMacro", L"ReleaseMacro", L"AbortMacro", L"IsKeyLockOn", L"SetBacklightColor",
        L"OutputDebugMessage", L"SetMouseDPITable", L"SetMouseDPITableIndex", L"EnablePrimaryMouseButtonEvents",
        L"EnableHidEvents", L"SetSteeringWheelProperty"};
    static const std::set<std::wstring> builtins = {
        L"string", L"table", L"math", L"os", L"coroutine", L"utf8", L"print", L"pairs", L"ipairs", L"next", L"type",
        L"tostring", L"tonumber", L"select", L"pcall", L"xpcall", L"error", L"assert", L"unpack", L"setmetatable",
        L"getmetatable", L"rawget", L"rawset", L"rawequal", L"rawlen", L"load", L"loadstring", L"self"};
    if (keywords.count(w)) return Tok::Keyword;
    if (api.count(w)) return Tok::Api;
    if (builtins.count(w)) return Tok::Builtin;
    return Tok::Default;
}

struct Span {
    LONG start, end;
    Tok kind;
};

std::vector<Span> lexLua(const std::wstring& s) {
    std::vector<Span> out;
    const size_t n = s.size();
    size_t i = 0;
    // "[[" or "[==[" at p: returns true and the number of '='
    auto longOpen = [&](size_t p, int& level) {
        if (p >= n || s[p] != L'[') return false;
        size_t q = p + 1;
        level = 0;
        while (q < n && s[q] == L'=') level++, q++;
        return q < n && s[q] == L'[';
    };
    auto longClose = [&](size_t from, int level) {
        for (size_t q = from; q < n; q++) {
            if (s[q] != L']') continue;
            size_t r = q + 1;
            int l = 0;
            while (r < n && s[r] == L'=') l++, r++;
            if (l == level && r < n && s[r] == L']') return r + 1;
        }
        return n;
    };
    auto push = [&](size_t a, size_t b, Tok k) { out.push_back({static_cast<LONG>(a), static_cast<LONG>(b), k}); };
    while (i < n) {
        const wchar_t c = s[i];
        int level = 0;
        if (c == L'-' && i + 1 < n && s[i + 1] == L'-') {
            const size_t st = i;
            if (longOpen(i + 2, level)) i = longClose(i + 4 + level, level);
            else
                while (i < n && s[i] != L'\r' && s[i] != L'\n') i++;
            push(st, i, Tok::Comment);
        } else if (c == L'"' || c == L'\'') {
            const size_t st = i++;
            while (i < n && s[i] != c && s[i] != L'\r' && s[i] != L'\n') i += s[i] == L'\\' ? 2 : 1;
            if (i < n && s[i] == c) i++;
            push(st, std::min(i, n), Tok::String);
        } else if (c == L'[' && longOpen(i, level)) {
            const size_t st = i;
            i = longClose(i + 2 + level, level);
            push(st, i, Tok::String);
        } else if (iswdigit(c) || (c == L'.' && i + 1 < n && iswdigit(s[i + 1]))) {
            const size_t st = i;
            if (c == L'0' && i + 1 < n && (s[i + 1] == L'x' || s[i + 1] == L'X')) {
                i += 2;
                while (i < n && (iswxdigit(s[i]) || s[i] == L'.')) i++;
            } else {
                while (i < n && (iswdigit(s[i]) || s[i] == L'.')) i++;
                if (i < n && (s[i] == L'e' || s[i] == L'E')) {
                    i++;
                    if (i < n && (s[i] == L'+' || s[i] == L'-')) i++;
                    while (i < n && iswdigit(s[i])) i++;
                }
            }
            push(st, i, Tok::Number);
        } else if (iswalpha(c) || c == L'_') {
            const size_t st = i;
            while (i < n && (iswalnum(s[i]) || s[i] == L'_')) i++;
            const Tok k = classifyWord(s.substr(st, i - st));
            if (k != Tok::Default) push(st, i, k);
        } else {
            i++;
        }
    }
    return out;
}

HWND editor() { return app.item(IDC_EDITOR); }

// crlf = true gives line breaks as "\r\n" (for saving); false gives the editor's own "\r".
std::wstring editorText(bool crlf) {
    GETTEXTLENGTHEX gl = {static_cast<DWORD>((crlf ? GTL_USECRLF : GTL_DEFAULT) | GTL_PRECISE | GTL_NUMCHARS), 1200};
    const LONG len = static_cast<LONG>(SendMessageW(editor(), EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&gl), 0));
    std::wstring buf(static_cast<size_t>(std::max<LONG>(len, 0)) + 1, L'\0');
    GETTEXTEX gt = {};
    gt.cb = static_cast<DWORD>(buf.size() * sizeof(wchar_t));
    gt.flags = crlf ? GT_USECRLF : GT_DEFAULT;
    gt.codepage = 1200;
    const LONG got = static_cast<LONG>(
        SendMessageW(editor(), EM_GETTEXTEX, reinterpret_cast<WPARAM>(&gt), reinterpret_cast<LPARAM>(&buf[0])));
    buf.resize(static_cast<size_t>(std::max<LONG>(got, 0)));
    return buf;
}

void setSpanFormat(HWND ed, LONG a, LONG b, COLORREF col, bool italic) {
    CHARRANGE r = {a, b};
    SendMessageW(ed, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&r));
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_ITALIC;
    cf.crTextColor = col;
    cf.dwEffects = italic ? CFE_ITALIC : 0;
    SendMessageW(ed, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
}

void highlightEditor() {
    HWND ed = editor();
    const std::wstring textNow = editorText(false);
    app.highlighting = true;
    long frozen = 0;
    if (app.editorDoc) {
        app.editorDoc->Undo(tomSuspend, nullptr);  // colouring shouldn't end up in the undo history
        app.editorDoc->Freeze(&frozen);
    }
    SendMessageW(ed, WM_SETREDRAW, FALSE, 0);
    CHARRANGE sel;
    SendMessageW(ed, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));
    POINT scroll = {};
    SendMessageW(ed, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));

    setSpanFormat(ed, 0, -1, tokColor(Tok::Default), false);
    for (const Span& sp : lexLua(textNow)) setSpanFormat(ed, sp.start, sp.end, tokColor(sp.kind), sp.kind == Tok::Comment);

    SendMessageW(ed, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&sel));
    SendMessageW(ed, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
    SendMessageW(ed, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(ed, nullptr, FALSE);
    if (app.editorDoc) {
        app.editorDoc->Unfreeze(&frozen);
        app.editorDoc->Undo(tomResume, nullptr);
    }
    app.highlighting = false;
}

std::string editorChunkName() { return app.scriptPath.empty() ? "untitled.lua" : fileName(app.scriptPath); }

// Unsaved-changes flag and the live syntax check follow the editor text.
void updateEditorState() {
    const std::wstring textNow = editorText(false);
    const bool dirty = app.untitled ? !textNow.empty() : textNow != app.editorSaved;
    std::string src = narrow(textNow);
    for (char& ch : src)
        if (ch == '\r') ch = '\n';
    const std::string err = src.empty() ? std::string() : Engine::checkSyntax(src, editorChunkName());
    if (dirty != app.editorDirty || err != app.syntaxError) {
        app.editorDirty = dirty;
        app.syntaxError = err;
        app.syntaxLine = Engine::errorLine(err);
        for (int id : {IDC_TAB_SCRIPT, IDC_SAVESCRIPT, IDC_REVERT}) InvalidateRect(app.item(id), nullptr, FALSE);
        InvalidateRect(app.wnd, &app.statusRect, FALSE);
        InvalidateRect(app.wnd, &app.gutterRect, FALSE);
        InvalidateRect(app.wnd, &app.heroRect, FALSE);
    }
    const bool canRevert = app.editorDirty && !app.untitled && !app.scriptPath.empty();
    HWND revert = app.item(IDC_REVERT);
    if (!canRevert && GetFocus() == revert) SetFocus(editor());
    EnableWindow(revert, canRevert);
}

void setEditorText(const std::wstring& t) {
    app.loadingEditor = true;
    SetWindowTextW(editor(), t.c_str());
    SendMessageW(editor(), EM_EMPTYUNDOBUFFER, 0, 0);
    app.loadingEditor = false;
    highlightEditor();
    app.editorSaved = editorText(false);
    app.editorDirty = false;
    updateEditorState();
    InvalidateRect(app.wnd, &app.gutterRect, FALSE);
}

void loadEditor(const std::string& path) {
    std::string bytes;
    if (!path.empty()) {
        if (FILE* f = openUtf8(path, "rb")) {
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), f)) > 0) bytes.append(buf, n);
            fclose(f);
        }
    }
    if (bytes.compare(0, 3, "\xEF\xBB\xBF") == 0) bytes.erase(0, 3);
    app.editorCrlf = bytes.find('\n') == std::string::npos || bytes.find("\r\n") != std::string::npos;
    // UTF-8, falling back to the Windows code page for old scripts
    std::wstring text;
    if (!bytes.empty()) {
        int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        const UINT cp = n > 0 ? CP_UTF8 : CP_ACP;
        n = MultiByteToWideChar(cp, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        text.assign(n, L'\0');
        MultiByteToWideChar(cp, 0, bytes.data(), static_cast<int>(bytes.size()), &text[0], n);
    }
    setEditorText(text);
}

void refreshChrome();
void stopScript();
void startScript();
std::string fileDialog(bool save, const wchar_t* filter, const std::string& initialDir, const std::string& initialName,
                       const wchar_t* defExt);
std::string ensureDir(const std::string& name);
void errorBox(const std::wstring& title, const std::string& msg);

// Writes the editor to disk (asking for a name for a new script). A running
// script is restarted with the new code when `reload` is set.
bool saveEditor(bool reload) {
    std::string path = app.scriptPath;
    if (path.empty() || app.untitled) {
        path = fileDialog(true, L"Lua scripts (*.lua)\0*.lua\0All files (*.*)\0*.*\0", ensureDir("scripts"), "my_script.lua",
                          L"lua");
        if (path.empty()) return false;
    }
    std::wstring textNow = editorText(true);
    if (!app.editorCrlf) {
        std::wstring lf;
        for (size_t i = 0; i < textNow.size(); i++)
            if (!(textNow[i] == L'\r' && i + 1 < textNow.size() && textNow[i + 1] == L'\n')) lf += textNow[i];
        textNow.swap(lf);
    }
    const std::string bytes = narrow(textNow);
    FILE* f = openUtf8(path, "wb");
    if (!f || fwrite(bytes.data(), 1, bytes.size(), f) != bytes.size()) {
        if (f) fclose(f);
        errorBox(L"Save script", "Cannot write " + path);
        return false;
    }
    fclose(f);
    app.scriptPath = path;
    app.untitled = false;
    app.editorSaved = editorText(false);
    app.editorDirty = true;  // force a refresh of everything that shows the saved state
    updateEditorState();
    app.log("Saved " + fileName(path) + "\n");
    refreshChrome();
    if (reload && app.running()) {
        stopScript();
        startScript();
    }
    return true;
}

bool confirmDiscardEdits() {
    if (!editor()) return true;
    updateEditorState();
    if (!app.editorDirty) return true;
    const std::wstring name = app.untitled || app.scriptPath.empty() ? L"the new script" : widen(fileName(app.scriptPath));
    const int r = MessageBoxW(app.wnd, (L"Save your changes to " + name + L"?").c_str(), L"Unsaved changes",
                              MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return false;
    if (r == IDYES) return saveEditor(false);
    return true;
}

void layout();

void showTab(int t) {
    app.tab = t;
    if (t == 1) app.logUnseen = app.logUnseenError = false;
    layout();
    if (t == 0) SetFocus(editor());
}

void gotoLine(int line) {
    const LONG ci = static_cast<LONG>(SendMessageW(editor(), EM_LINEINDEX, std::max(line - 1, 0), 0));
    if (ci < 0) return;
    CHARRANGE r = {ci, ci};
    SendMessageW(editor(), EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&r));
    SendMessageW(editor(), EM_SCROLLCARET, 0, 0);
    SetFocus(editor());
}

void newScript() {
    if (!confirmDiscardEdits()) return;
    app.scriptPath.clear();
    app.untitled = true;
    setEditorText(
        L"-- New script. Press Save (Ctrl+S) to keep it, then Start (F5).\r"
        L"function OnEvent(event, arg, family)\r"
        L"    if event == \"PROFILE_ACTIVATED\" then\r"
        L"        OutputLogMessage(\"Script started\\n\")\r"
        L"    end\r"
        L"\r"
        L"    if event == \"MOUSE_BUTTON_PRESSED\" and arg == 4 then\r"
        L"        OutputLogMessage(\"Back button pressed\\n\")\r"
        L"    end\r"
        L"end\r");
    updateEditorState();
    refreshChrome();
    showTab(0);
    gotoLine(7);
}

void revertScript() {
    updateEditorState();
    if (app.untitled || app.scriptPath.empty() || !app.editorDirty) return;
    if (MessageBoxW(app.wnd, (L"Throw away your changes to " + widen(fileName(app.scriptPath)) + L"?").c_str(),
                    L"Revert", MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
        return;
    loadEditor(app.scriptPath);
    refreshChrome();
}

// Enter keeps the indentation of the current line, and indents one more step
// after lines that open a block (then, do, else, repeat, function ...).
void newlineWithIndent(HWND ed) {
    CHARRANGE sel;
    SendMessageW(ed, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));
    const LONG line = static_cast<LONG>(SendMessageW(ed, EM_EXLINEFROMCHAR, 0, sel.cpMin));
    const LONG start = static_cast<LONG>(SendMessageW(ed, EM_LINEINDEX, line, 0));
    wchar_t buf[2048];
    *reinterpret_cast<WORD*>(buf) = 2047;
    LONG len = static_cast<LONG>(SendMessageW(ed, EM_GETLINE, line, reinterpret_cast<LPARAM>(buf)));
    std::wstring ln(buf, static_cast<size_t>(std::max<LONG>(0, std::min(len, sel.cpMin - start))));
    while (!ln.empty() && (ln.back() == L'\r' || ln.back() == L'\n')) ln.pop_back();
    size_t ws = 0;
    while (ws < ln.size() && (ln[ws] == L' ' || ln[ws] == L'\t')) ws++;
    std::wstring indent = ln.substr(0, ws);
    std::wstring t = ln;
    while (!t.empty() && iswspace(t.back())) t.pop_back();
    auto endsWord = [&](const wchar_t* w) {
        const size_t k = wcslen(w);
        return t.size() >= k && t.compare(t.size() - k, k, w) == 0 &&
               (t.size() == k || !(iswalnum(t[t.size() - k - 1]) || t[t.size() - k - 1] == L'_'));
    };
    const bool opensFunction = t.find(L"function") != std::wstring::npos && !t.empty() && t.back() == L')';
    if (endsWord(L"then") || endsWord(L"do") || endsWord(L"else") || endsWord(L"repeat") || opensFunction ||
        (!t.empty() && t.back() == L'{'))
        indent += L"    ";
    SendMessageW(ed, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>((L"\r" + indent).c_str()));
}

// Repaint the line numbers when the editor scrolled or the caret moved to another line.
void checkGutter() {
    HWND ed = editor();
    const LONG first = static_cast<LONG>(SendMessageW(ed, EM_GETFIRSTVISIBLELINE, 0, 0));
    CHARRANGE sel;
    SendMessageW(ed, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));
    const LONG caret = static_cast<LONG>(SendMessageW(ed, EM_EXLINEFROMCHAR, 0, sel.cpMin));
    if (first != app.gutterFirst || caret != app.gutterCaret) {
        app.gutterFirst = first;
        app.gutterCaret = caret;
        InvalidateRect(app.wnd, &app.gutterRect, FALSE);
    }
}

LRESULT CALLBACK editorProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    switch (msg) {
        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTCHARS | DLGC_WANTTAB | DLGC_HASSETSEL;
        case WM_KEYDOWN:
            if (wp == VK_TAB && !(GetKeyState(VK_CONTROL) & 0x8000)) {
                if (!(GetKeyState(VK_SHIFT) & 0x8000))
                    SendMessageW(h, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"    "));
                return 0;
            }
            if (wp == VK_RETURN) {
                newlineWithIndent(h);
                return 0;
            }
            break;
        case WM_CHAR:
            if (wp == L'\t' || wp == L'\r' || wp == L'\n') return 0;  // handled in WM_KEYDOWN
            break;
        case WM_PASTE:
            SendMessageW(h, EM_PASTESPECIAL, CF_UNICODETEXT, 0);  // paste plain text, not formatting
            return 0;
    }
    const LRESULT r = DefSubclassProc(h, msg, wp, lp);
    if (msg == WM_KEYDOWN || msg == WM_MOUSEWHEEL || msg == WM_VSCROLL || msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP)
        checkGutter();
    return r;
}

// ------------------------------------------------------------------ dropdown lists

// A dark popup list that replaces native menus: returns the chosen index or -1.
// Items may carry a right-aligned hint after a tab ("Save\tCtrl+S"); "" is a separator.
struct Popup {
    std::vector<std::wstring> items;
    std::vector<bool> enabled;
    std::vector<RECT> rects;
    int checked = -1, hover = -1, result = -1;
    bool done = false;
} *g_popup = nullptr;

int popupItemAt(POINT p) {
    for (size_t i = 0; i < g_popup->rects.size(); i++)
        if (!g_popup->items[i].empty() && g_popup->enabled[i] && PtInRect(&g_popup->rects[i], p))
            return static_cast<int>(i);
    return -1;
}

void popupPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC target = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, rc.right, rc.bottom);
    HGDIOBJ old = SelectObject(dc, bmp);
    {
        Canvas c(dc);
        HBRUSH bg = CreateSolidBrush(RGB(0x1b, 0x1f, 0x27));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);
        strokeRound(c, rc, 0, Color::borderHover);
        for (size_t i = 0; i < g_popup->items.size(); i++) {
            const RECT& r = g_popup->rects[i];
            const std::wstring& item = g_popup->items[i];
            if (item.empty()) {
                RECT line = {r.left + app.S(10), (r.top + r.bottom) / 2, r.right - app.S(10), (r.top + r.bottom) / 2 + 1};
                fillRound(c, line, 0, Color::border);
                continue;
            }
            if (static_cast<int>(i) == g_popup->hover) {
                RECT h = {r.left + app.S(5), r.top + app.S(1), r.right - app.S(5), r.bottom - app.S(1)};
                fillRound(c, h, app.Sf(6), Color::fieldHover);
            }
            const bool on = g_popup->enabled[i];
            if (static_cast<int>(i) == g_popup->checked) {
                RECT ib = {r.left + app.S(12), r.top + app.S(9), r.left + app.S(26), r.bottom - app.S(9)};
                drawIcon(c, Icon::Check, ib, Color::accent, app.Sf(1.8f));
            }
            const size_t tab = item.find(L'\t');
            RECT tr = {r.left + app.S(34), r.top, r.right - app.S(14), r.bottom};
            text(c, item.substr(0, tab), tr, app.font, on ? Color::text : Color::faint);
            if (tab != std::wstring::npos)
                text(c, item.substr(tab + 1), tr, app.fontSmall, on ? Color::muted : Color::faint, DT_RIGHT | DT_VCENTER);
        }
    }
    BitBlt(target, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK popupProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (!g_popup) return DefWindowProcW(hwnd, msg, wp, lp);
    POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    RECT rc;
    GetClientRect(hwnd, &rc);
    switch (msg) {
        case WM_PAINT:
            popupPaint(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE: {
            const int h = popupItemAt(p);
            if (h != g_popup->hover) {
                g_popup->hover = h;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
            if (!PtInRect(&rc, p)) g_popup->done = true;  // click outside closes
            return 0;
        case WM_LBUTTONUP: {
            const int i = popupItemAt(p);
            if (i >= 0) {
                g_popup->result = i;
                g_popup->done = true;
            }
            return 0;
        }
        case WM_KEYDOWN: {
            const int n = static_cast<int>(g_popup->items.size());
            if (wp == VK_ESCAPE) g_popup->done = true;
            if (wp == VK_RETURN && g_popup->hover >= 0) {
                g_popup->result = g_popup->hover;
                g_popup->done = true;
            }
            if (wp == VK_DOWN || wp == VK_UP) {
                const int step = wp == VK_DOWN ? 1 : -1;
                int i = g_popup->hover;
                for (int tries = 0; tries < n; tries++) {
                    i = (i + step + n) % n;
                    if (!g_popup->items[i].empty() && g_popup->enabled[i]) break;
                }
                g_popup->hover = i;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (reinterpret_cast<HWND>(lp) != hwnd) g_popup->done = true;
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) g_popup->done = true;
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int popupList(const RECT& anchor, const std::vector<std::wstring>& items, int checked,
              const std::vector<bool>& enabled = {}) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {sizeof(wc)};
        wc.style = CS_DROPSHADOW;
        wc.lpfnWndProc = popupProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"LSBPopup";
        RegisterClassExW(&wc);
        registered = true;
    }
    Popup st;
    st.items = items;
    st.enabled = enabled.size() == items.size() ? enabled : std::vector<bool>(items.size(), true);
    st.checked = checked;

    // size: widest item or the anchor, whichever is wider
    HDC dc = GetDC(app.wnd);
    int width = anchor.right - anchor.left, y = app.S(5);
    for (size_t i = 0; i < items.size(); i++) {
        const int h = items[i].empty() ? app.S(9) : app.S(32);
        st.rects.push_back({0, y, 0, y + h});
        y += h;
        const size_t tab = items[i].find(L'\t');
        int w = textWidth(dc, items[i].substr(0, tab), app.font) + app.S(60);
        if (tab != std::wstring::npos) w += textWidth(dc, items[i].substr(tab + 1), app.fontSmall) + app.S(24);
        width = std::max(width, w);
    }
    ReleaseDC(app.wnd, dc);
    const int height = y + app.S(5);
    for (auto& r : st.rects) r.right = width;

    // place below the anchor, or above it if there's no room
    POINT pos = {anchor.left, anchor.bottom + app.S(4)};
    ClientToScreen(app.wnd, &pos);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(app.wnd, MONITOR_DEFAULTTONEAREST), &mi);
    if (pos.y + height > mi.rcWork.bottom) {
        POINT top = {anchor.left, anchor.top - app.S(4) - height};
        ClientToScreen(app.wnd, &top);
        pos.y = top.y;
    }
    pos.x = std::min(pos.x, static_cast<LONG>(mi.rcWork.right - width));

    g_popup = &st;
    HWND pw = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"LSBPopup", L"", WS_POPUP, pos.x, pos.y, width, height,
                              app.wnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    const int corner = 3;  // DWMWCP_ROUNDSMALL on Windows 11
    DwmSetWindowAttribute(pw, 33, &corner, sizeof(corner));
    ShowWindow(pw, SW_SHOW);
    SetFocus(pw);
    SetCapture(pw);
    MSG msg;
    while (!st.done) {
        const BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r <= 0) {
            if (r == 0) PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (GetCapture() == pw) ReleaseCapture();
    g_popup = nullptr;
    DestroyWindow(pw);
    SetForegroundWindow(app.wnd);
    return st.result;
}

// ------------------------------------------------------------------ dialogs

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
    if (app.status == App::Status::Running) setStatus(App::Status::Stopped);
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
    updateEditorState();
    if (app.editorDirty && !saveEditor(false)) return;  // run what's in the editor
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

    app.engine = std::make_unique<Engine>(*app.backend, app.input,
                                          [](const std::string& s) { app.log(s, engineLogKind(s)); }, app.fallback.get());
    app.engine->onClearLog = [] { PostMessageW(app.wnd, WM_APP_CLEARLOG, 0, 0); };
    app.engine->setJitter(c.jitterMin, c.jitterMax);

    app.log(std::string("\nStarting ") + fileName(c.script) + " on " + info.label + "\n");
    if (!app.engine->start(source, fileName(c.script), err)) {
        app.engine.reset();
        closeDevices();
        app.log("Could not load script:\n" + err + "\n", LogKind::Error);
        setStatus(App::Status::Error);
        flushLog();
        refreshChrome();
        if (const int line = Engine::errorLine(err)) {
            if (app.tab != 0) showTab(0);
            gotoLine(line);
        }
        return;
    }
    if (!app.input.startHook(app.engine.get(), c.extraKeys, err)) {
        stopScript();
        errorBox(L"Input", err);
        return;
    }
    if (c.jitterMax > 0)
        app.log("Randomizing mouse movement: " + narrow(formatPx(std::min(c.jitterMin, c.jitterMax))) + "–" +
                narrow(formatPx(std::max(c.jitterMin, c.jitterMax))) + " px off the exact path.\n");
    if (c.extraKeys == "mouse") app.log("F13–F24 act as mouse buttons 6–17.\n");
    if (c.extraKeys == "gkeys") app.log("F13–F24 act as G-keys G1–G12.\n");
    setStatus(App::Status::Running);
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
    std::string name = app.configPath.empty() ? fileName(app.scriptPath) : fileName(app.configPath);
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".lua") == 0) name = name.substr(0, name.size() - 4);
    if (name.empty()) name = "my config";
    if (name.size() < 4 || name.compare(name.size() - 4, 4, ".ini") != 0) name += ".ini";
    const std::string dir = app.configPath.empty() ? ensureDir("configs") : dirName(app.configPath);
    const std::string path = fileDialog(true, L"Bridge config (*.ini)\0*.ini\0All files (*.*)\0*.*\0", dir, name, L"ini");
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

void loadConfigDialog() {
    const std::string path = fileDialog(false, L"Bridge config (*.ini)\0*.ini\0All files (*.*)\0*.*\0",
                                        app.configPath.empty() ? ensureDir("configs") : dirName(app.configPath), "", L"ini");
    if (!path.empty()) loadConfigFile(path);
}

void openScript() {
    const std::string path = fileDialog(false, L"Lua scripts (*.lua)\0*.lua\0All files (*.*)\0*.*\0",
                                        app.scriptPath.empty() ? ensureDir("scripts") : dirName(app.scriptPath), "", L"lua");
    if (!path.empty()) setScript(path);
}

void libraryMenu() {
    std::vector<std::wstring> items;
    std::vector<std::string> paths;
    for (const char* folder : {"scripts", "examples"}) {
        const auto files = listLua(joinPath(app.exeDir, folder));
        if (!files.empty() && !items.empty()) {
            items.push_back(L"");
            paths.push_back("");
        }
        for (const auto& f : files) {
            items.push_back(widen(fileName(f)) + L"\t" + widen(folder));
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
        if (!paths[i].empty() && paths[i] == app.scriptPath) checked = static_cast<int>(i);
    const int i = popupList(clientRectOf(IDC_LIBRARY), items, checked);
    if (i < 0) return;
    if (paths[i] == "*folder")
        ShellExecuteW(app.wnd, L"open", widen(ensureDir("scripts")).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    else if (!paths[i].empty())
        setScript(paths[i]);
}

void configMenu() {
    const int i = popupList(clientRectOf(IDC_CONFIG),
                            {L"Save\tCtrl+S", L"Save as…\tCtrl+Shift+S", L"", L"Load…\tCtrl+L", L"Open the configs folder"},
                            -1, {true, true, false, !app.running(), true});
    if (i == 0) saveConfigFile();
    if (i == 1) saveConfigAs();
    if (i == 3) loadConfigDialog();
    if (i == 4) ShellExecuteW(app.wnd, L"open", widen(ensureDir("configs")).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
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
    const int i = popupList(app.fields[IDC_PORT], items, checked, enabled);
    if (i >= 0 && i < static_cast<int>(ports.size())) SetWindowTextW(app.item(IDC_PORT), items[i].c_str());
}

void deviceMenu() {
    std::vector<std::wstring> items;
    for (const auto& d : kDevices) items.push_back(widen(d.label));
    const int i = popupList(clientRectOf(IDC_DEVICE), items, app.device);
    if (i < 0) return;
    app.device = i;
    refreshChrome();
}

void hotkeyMenu() {
    std::vector<std::wstring> items;
    for (const auto& h : kHotkeys) items.push_back(h.label);
    const int i = popupList(clientRectOf(IDC_HOTKEY), items, app.hotkey);
    if (i < 0) return;
    app.hotkey = i;
    registerHotkey();
    InvalidateRect(app.item(IDC_HOTKEY), nullptr, FALSE);
    refreshChrome();
}

void extraMenu() {
    const int i = popupList(clientRectOf(IDC_EXTRA), {kExtraLabels[0], kExtraLabels[1], kExtraLabels[2]}, app.extraKeys);
    if (i < 0) return;
    app.extraKeys = i;
    InvalidateRect(app.item(IDC_EXTRA), nullptr, FALSE);
    refreshChrome();
}

void toggle(int id, bool& value) {
    const float from = value ? 1.0f : 0.0f;
    value = !value;
    app.anims[id] = {from, value ? 1.0f : 0.0f, GetTickCount()};
    InvalidateRect(app.item(id), nullptr, FALSE);
    refreshChrome();
}

float togglePos(int id, bool value) {
    auto it = app.anims.find(id);
    if (it == app.anims.end()) return value ? 1.0f : 0.0f;
    const float t = std::min(1.0f, (GetTickCount() - it->second.start) / 160.0f);
    const float e = 1 - (1 - t) * (1 - t) * (1 - t);  // ease out
    return it->second.from + (it->second.to - it->second.from) * e;
}

// ------------------------------------------------------------------ backdrop (static parts)

void paintBackdrop(Canvas& c, const RECT& client) {
    {
        Gdiplus::RectF all = c.rf(client);
        all.Inflate(1, 1);
        Gdiplus::LinearGradientBrush bg(all, gp(Color::bgTop), gp(Color::bgBottom), 90.0f);
        c.g.FillRectangle(&bg, all);
    }
    glow(c, client.right * 0.18f, 0, app.Sf(420), Color::accent, 22);
    glow(c, client.right * 0.9f, client.bottom * 0.35f, app.Sf(360), Color::accent2, 12);

    // title bar separator
    RECT sep = {0, app.titleH - 1, client.right, app.titleH};
    fillRound(c, sep, 0, Color::white, 10);

    // hero
    const COLORREF heroEdge = app.status == App::Status::Running ? Color::success
                              : app.status == App::Status::Error ? Color::danger : Color::cardBorder;
    softShadow(c, app.heroRect, app.Sf(14), app.S(10));
    gradientRound(c, app.heroRect, app.Sf(14), RGB(0x1c, 0x24, 0x3f), RGB(0x17, 0x19, 0x24), 25);
    {
        Gdiplus::GraphicsPath clip;
        roundPath(clip, c.rf(app.heroRect), app.Sf(14));
        c.g.SetClip(&clip);
        RECT btn = clientRectOf(IDC_START);
        const COLORREF glowCol = app.status == App::Status::Running ? Color::danger : Color::accent2;
        glow(c, (btn.left + btn.right) / 2.0f, (btn.top + btn.bottom) / 2.0f, app.Sf(170), glowCol, 55);
        glow(c, static_cast<float>(app.heroRect.left), static_cast<float>(app.heroRect.top), app.Sf(260), Color::accent, 30);
        c.g.ResetClip();
    }
    strokeRound(c, app.heroRect, app.Sf(14), heroEdge, 1, app.status == App::Status::Stopped ? 255 : 150);

    // cards
    for (const auto& card : app.cards) {
        softShadow(c, card.rect, app.Sf(12), app.S(8));
        fillRound(c, card.rect, app.Sf(12), Color::card);
        strokeRound(c, card.rect, app.Sf(12), Color::cardBorder);
    }

    // device help callout
    if (!IsRectEmpty(&app.helpRect)) {
        fillRound(c, app.helpRect, app.Sf(9), Color::accent, 20);
        strokeRound(c, app.helpRect, app.Sf(9), Color::accent, 1, 45);
    }

    // Script / Log tab strip
    fillRound(c, app.tabsRect, app.Sf(9), Color::field);
    strokeRound(c, app.tabsRect, app.Sf(9), Color::border);

    // boxes behind edit controls
    HWND focus = GetFocus();
    for (const auto& f : app.fields) {
        const bool isCode = f.first == IDC_LOG || f.first == IDC_EDITOR;
        fillRound(c, f.second, app.Sf(8), isCode ? Color::logBg : Color::field);
        const bool focused = !isCode && focus == app.item(f.first);
        strokeRound(c, f.second, app.Sf(8), focused ? Color::accent : Color::border, focused ? app.Sf(1.5f) : 1);
    }
    if (app.tab == 0 && !IsRectEmpty(&app.gutterRect)) {
        RECT line = {app.gutterRect.right - 1, app.gutterRect.top + app.S(1), app.gutterRect.right,
                     app.gutterRect.bottom - app.S(1)};
        fillRound(c, line, 0, Color::border);
    }
}

void ensureBackdrop() {
    RECT rc;
    GetClientRect(app.wnd, &rc);
    const SIZE size = {std::max<LONG>(rc.right, 1), std::max<LONG>(rc.bottom, 1)};
    if (!app.backdropDC || size.cx != app.backdropSize.cx || size.cy != app.backdropSize.cy) {
        if (app.backdropDC) {
            DeleteDC(app.backdropDC);
            DeleteObject(app.backdropBmp);
        }
        HDC screen = GetDC(nullptr);
        app.backdropDC = CreateCompatibleDC(screen);
        app.backdropBmp = CreateCompatibleBitmap(screen, size.cx, size.cy);
        ReleaseDC(nullptr, screen);
        SelectObject(app.backdropDC, app.backdropBmp);
        app.backdropSize = size;
        app.backdropDirty = true;
    }
    if (app.backdropDirty) {
        Canvas c(app.backdropDC);
        paintBackdrop(c, rc);
        app.backdropDirty = false;
    }
}

// ------------------------------------------------------------------ controls

void drawButton(const DRAWITEMSTRUCT* di) {
    const int id = static_cast<int>(di->CtlID);
    const ButtonInfo info = app.buttons[id];
    const bool disabled = di->itemState & ODS_DISABLED, pressed = di->itemState & ODS_SELECTED;
    const bool hover = di->hwndItem == app.hover && !disabled;
    const bool focus = (di->itemState & ODS_FOCUS) && !(di->itemState & ODS_NOFOCUSRECT);
    const RECT wr = clientRectOf(id);
    const int w = wr.right - wr.left, h = wr.bottom - wr.top;
    if (w <= 0 || h <= 0) return;

    HDC mem = CreateCompatibleDC(di->hDC);
    HBITMAP bmp = CreateCompatibleBitmap(di->hDC, w, h);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    ensureBackdrop();
    BitBlt(mem, 0, 0, w, h, app.backdropDC, wr.left, wr.top, SRCCOPY);

    wchar_t labelBuf[128] = L"";
    GetWindowTextW(di->hwndItem, labelBuf, 128);
    const std::wstring label = labelBuf;
    {
        Canvas c(mem, wr.left, wr.top);
        const float radius = app.Sf(8);
        const COLORREF fg = disabled ? Color::faint : Color::text;
        switch (info.role) {
            case Role::Hero: {
                const bool run = app.running();
                COLORREF a = run ? Color::danger : Color::accent, b = run ? RGB(0xd9, 0x46, 0x8f) : Color::accent2;
                if (hover) a = mix(a, Color::white, 0.12), b = mix(b, Color::white, 0.12);
                if (pressed) a = mix(a, Color::black, 0.15), b = mix(b, Color::black, 0.15);
                const float d = static_cast<float>(std::min(w, h)) - app.Sf(4);
                const float x = wr.left + (w - d) / 2.0f - c.ox, y = wr.top + (h - d) / 2.0f - c.oy;
                Gdiplus::GraphicsPath circle;
                circle.AddEllipse(x, y, d, d);
                Gdiplus::LinearGradientBrush br(Gdiplus::RectF(x - 1, y - 1, d + 2, d + 2), gp(a), gp(b), 45.0f);
                c.g.FillPath(&br, &circle);
                Gdiplus::Pen rim(gp(Color::white, 40), app.Sf(1.2f));
                c.g.DrawEllipse(&rim, x + 1, y + 1, d - 2, d - 2);
                const int is = static_cast<int>(d * 0.36f);
                RECT ib = {wr.left + (w - is) / 2 + (run ? 0 : app.S(2)), wr.top + (h - is) / 2,
                           wr.left + (w - is) / 2 + is + (run ? 0 : app.S(2)), wr.top + (h - is) / 2 + is};
                drawIcon(c, run ? Icon::Stop : Icon::Play, ib, Color::white, app.Sf(2));
                if (focus) {
                    Gdiplus::Pen ring(gp(Color::white, 120), app.Sf(1.5f));
                    c.g.DrawEllipse(&ring, x - app.Sf(3), y - app.Sf(3), d + app.Sf(6), d + app.Sf(6));
                }
                break;
            }
            case Role::Ghost:
            case Role::Secondary: {
                const bool emphasis = id == IDC_SAVESCRIPT && app.editorDirty && !disabled;
                if (emphasis) {
                    gradientRound(c, wr, radius, hover ? Color::accentHover : Color::accent, Color::accent2, 30);
                } else if (info.role == Role::Secondary) {
                    fillRound(c, wr, radius, hover ? Color::fieldHover : Color::field);
                    strokeRound(c, wr, radius, hover ? Color::borderHover : Color::border);
                } else if (hover || pressed) {
                    fillRound(c, wr, radius, Color::white, pressed ? 26 : 14);
                }
                if (pressed && info.role == Role::Secondary) fillRound(c, wr, radius, Color::black, 40);
                const int is = app.S(16), gap = label.empty() ? 0 : app.S(8);
                const int tw = label.empty() ? 0 : textWidth(mem, label, app.font);
                const int total = (info.icon != Icon::None ? is : 0) + gap + tw;
                const int x = wr.left + (w - total) / 2;
                if (info.icon != Icon::None) {
                    RECT ib = {x, wr.top + (h - is) / 2, x + is, wr.top + (h - is) / 2 + is};
                    drawIcon(c, info.icon, ib,
                             emphasis ? Color::white : disabled ? Color::faint : hover ? Color::white : Color::muted,
                             app.Sf(1.6f));
                }
                if (!label.empty()) {
                    RECT tr = {x + (info.icon != Icon::None ? is + gap : 0), wr.top, wr.right, wr.bottom};
                    text(c, label, tr, emphasis ? app.fontBold : app.font, emphasis ? Color::white : fg);
                }
                if (focus) strokeRound(c, wr, radius, Color::accent, app.Sf(1.5f), 180);
                break;
            }
            case Role::Dropdown: {
                fillRound(c, wr, radius, hover ? Color::fieldHover : Color::field);
                strokeRound(c, wr, radius, focus ? Color::accent : hover ? Color::borderHover : Color::border,
                            focus ? app.Sf(1.5f) : 1);
                const std::wstring value = id == IDC_DEVICE   ? widen(kDevices[app.device].label)
                                           : id == IDC_HOTKEY ? std::wstring(kHotkeys[app.hotkey].label)
                                                              : std::wstring(kExtraLabels[app.extraKeys]);
                RECT tr = {wr.left + app.S(12), wr.top, wr.right - app.S(34), wr.bottom};
                text(c, value, tr, app.font, fg);
                RECT ib = {wr.right - app.S(28), wr.top + (h - app.S(16)) / 2, wr.right - app.S(12),
                           wr.top + (h + app.S(16)) / 2};
                drawIcon(c, Icon::Chevron, ib, disabled ? Color::faint : Color::muted, app.Sf(1.6f));
                break;
            }
            case Role::Chevron: {
                if (hover || pressed) {
                    RECT hr = wr;
                    InflateRect(&hr, -app.S(3), -app.S(3));
                    fillRound(c, hr, app.Sf(6), pressed ? Color::border : Color::fieldHover);
                }
                RECT ib = {wr.left + (w - app.S(16)) / 2, wr.top + (h - app.S(16)) / 2, wr.left + (w + app.S(16)) / 2,
                           wr.top + (h + app.S(16)) / 2};
                drawIcon(c, info.icon == Icon::None ? Icon::Chevron : info.icon, ib,
                         disabled ? Color::faint : Color::muted, app.Sf(1.6f));
                break;
            }
            case Role::Toggle: {
                const bool on = id == IDC_KEYFALLBACK ? app.keyFallback : app.autoStart;
                const float pos = togglePos(id, on);
                const int tw = app.S(40), th = app.S(22);
                RECT track = {wr.left, wr.top + (h - th) / 2, wr.left + tw, wr.top + (h - th) / 2 + th};
                COLORREF off = hover ? Color::borderHover : Color::border;
                COLORREF col = mix(off, hover ? Color::accentHover : Color::accent, pos);
                if (disabled) col = mix(col, Color::card, 0.5);
                fillRound(c, track, th / 2.0f, col);
                const float knob = th / 2.0f - app.Sf(3.5f);
                const float kx = track.left + th / 2.0f + pos * (tw - th) - c.ox, ky = track.top + th / 2.0f - c.oy;
                Gdiplus::SolidBrush kb(gp(disabled ? Color::muted : Color::white));
                c.g.FillEllipse(&kb, kx - knob, ky - knob, knob * 2, knob * 2);
                if (focus) strokeRound(c, track, th / 2.0f, Color::accentHover, app.Sf(1.5f));
                RECT tr = {track.right + app.S(12), wr.top, wr.right, wr.bottom};
                text(c, label, tr, app.font, disabled ? Color::faint : Color::text);
                break;
            }
            case Role::Caption:
            case Role::CaptionClose: {
                if (hover || pressed) {
                    if (info.role == Role::CaptionClose) fillRound(c, wr, 0, Color::danger, pressed ? 200 : 255);
                    else fillRound(c, wr, 0, Color::white, pressed ? 24 : 14);
                }
                const Icon glyph = id == IDC_MAX && IsZoomed(app.wnd) ? Icon::Restore : info.icon;
                const int is = app.S(18);
                RECT ib = {wr.left + (w - is) / 2, wr.top + (h - is) / 2, wr.left + (w + is) / 2, wr.top + (h + is) / 2};
                drawIcon(c, glyph, ib, hover ? Color::white : Color::text, app.Sf(1.2f));
                break;
            }
            case Role::Tab: {
                const bool selected = (id == IDC_TAB_SCRIPT) == (app.tab == 0);
                if (selected) {
                    fillRound(c, wr, app.Sf(7), Color::white, 20);
                    strokeRound(c, wr, app.Sf(7), Color::white, 1, 18);
                } else if (hover) {
                    fillRound(c, wr, app.Sf(7), Color::white, 8);
                }
                // a dot for unsaved edits / unseen log output
                COLORREF dot = CLR_INVALID;
                if (id == IDC_TAB_SCRIPT && app.editorDirty) dot = Color::accent;
                if (id == IDC_TAB_LOG && app.logUnseen) dot = app.logUnseenError ? Color::danger : Color::muted;
                HFONT f = selected ? app.fontBold : app.font;
                const int tw = textWidth(mem, label, f), ds = dot == CLR_INVALID ? 0 : app.S(12);
                const int x = wr.left + (w - tw - ds) / 2;
                RECT tr = {x, wr.top, x + tw + app.S(2), wr.bottom};
                text(c, label, tr, f, selected ? Color::white : hover ? Color::text : Color::muted);
                if (dot != CLR_INVALID) {
                    Gdiplus::SolidBrush db(gp(dot));
                    c.g.FillEllipse(&db, x + tw + app.Sf(6) - c.ox, wr.top + h / 2.0f - app.Sf(3) - c.oy, app.Sf(6), app.Sf(6));
                }
                break;
            }
            case Role::Chip: {
                const float r = h / 2.0f;
                fillRound(c, wr, r, hover ? Color::fieldHover : Color::field, 230);
                strokeRound(c, wr, r, hover ? Color::borderHover : Color::border);
                const bool dirty = modified();
                const COLORREF dot = app.configPath.empty() ? Color::faint : dirty ? Color::accent : Color::success;
                Gdiplus::SolidBrush db(gp(dot));
                c.g.FillEllipse(&db, wr.left + app.Sf(12) - c.ox, wr.top + h / 2.0f - app.Sf(3.5f) - c.oy, app.Sf(7),
                                app.Sf(7));
                const std::wstring name = app.configPath.empty() ? L"No config" : widen(fileName(app.configPath));
                RECT tr = {wr.left + app.S(26), wr.top, wr.right - (dirty ? app.S(84) : app.S(30)), wr.bottom};
                text(c, name, tr, app.fontBold, app.configPath.empty() ? Color::muted : Color::text);
                RECT st = {wr.right - app.S(84), wr.top, wr.right - app.S(28), wr.bottom};
                if (dirty) text(c, L"unsaved", st, app.fontSmall, Color::accent, DT_RIGHT | DT_VCENTER);
                RECT ib = {wr.right - app.S(24), wr.top + (h - app.S(14)) / 2, wr.right - app.S(10),
                           wr.top + (h + app.S(14)) / 2};
                drawIcon(c, Icon::Chevron, ib, Color::muted, app.Sf(1.6f));
                break;
            }
        }
    }
    BitBlt(di->hDC, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

// ------------------------------------------------------------------ window painting

// Mouse picture whose buttons light up: filled = pressed on the mouse, outlined = held by the script.
void paintMouse(Canvas& c) {
    if (IsRectEmpty(&app.mouseCard)) return;
    bool phys[6] = {}, script[6] = {};
    for (int n = 1; n <= 5 && app.engine; n++) app.engine->buttonState(n, phys[n], script[n]);
    const bool live = app.running();

    const float bw = app.Sf(106), bh = app.Sf(166);
    const RECT& a = app.mouseArea;
    const float bx = a.left + (a.right - a.left - bw) / 2.0f + app.Sf(8) - c.ox;
    const float by = a.top + (a.bottom - a.top - bh) / 2.0f - c.oy;
    const float r2 = app.Sf(44), splitY = by + bh * 0.42f, cx = bx + bw / 2;

    Gdiplus::GraphicsPath body;
    body.AddArc(bx, by, bw, bw, 180, 180);
    body.AddArc(bx + bw - 2 * r2, by + bh - 2 * r2, 2 * r2, 2 * r2, 0, 90);
    body.AddArc(bx, by + bh - 2 * r2, 2 * r2, 2 * r2, 90, 90);
    body.CloseFigure();

    if (live) glow(c, cx + c.ox, by + bh / 2 + c.oy, app.Sf(110), Color::accent, 26);
    {
        Gdiplus::LinearGradientBrush fill(Gdiplus::RectF(bx, by, bw, bh), gp(RGB(0x26, 0x2b, 0x36)),
                                          gp(RGB(0x1a, 0x1e, 0x26)), 90.0f);
        c.g.FillPath(&fill, &body);
    }

    // how a button part looks: pressed on the mouse, held by the script, or idle
    struct Look {
        std::unique_ptr<Gdiplus::Brush> brush;
        COLORREF line, number;
        float width;
    };
    auto look = [&](int n, const Gdiplus::RectF& box) {
        Look l;
        l.width = 1;
        if (phys[n]) {
            l.brush.reset(new Gdiplus::LinearGradientBrush(box, gp(Color::accent), gp(Color::accent2), 60.0f));
            l.line = Color::accentHover;
            l.number = Color::white;
        } else if (script[n]) {
            l.brush.reset(new Gdiplus::SolidBrush(gp(mix(Color::field, Color::accent, 0.3))));
            l.line = Color::accentHover;
            l.width = app.Sf(2);
            l.number = Color::white;
        } else {
            l.brush.reset(new Gdiplus::SolidBrush(gp(Color::black, 0)));
            l.line = live ? Color::borderHover : Color::border;
            l.number = live ? Color::muted : Color::faint;
        }
        return l;
    };

    // left and right buttons: the body clipped to each top quarter
    for (int side = 0; side < 2; side++) {
        const int n = side == 0 ? 1 : 2;
        Gdiplus::RectF box(side == 0 ? bx : cx, by, bw / 2, splitY - by);
        Gdiplus::Region reg(&body);
        reg.Intersect(box);
        Look l = look(n, box);
        c.g.FillRegion(l.brush.get(), &reg);
        RECT nr = {static_cast<LONG>(box.X + c.ox), static_cast<LONG>(box.Y + box.Height * 0.45f + c.oy),
                   static_cast<LONG>(box.X + box.Width + c.ox), static_cast<LONG>(splitY + c.oy)};
        if (side == 0) nr.right -= app.S(6);
        else nr.left += app.S(6);
        text(c, side == 0 ? L"1" : L"2", nr, app.fontSmall, l.number, DT_CENTER | DT_VCENTER);
    }
    {
        Gdiplus::Pen outline(gp(live ? Color::borderHover : Color::border), app.Sf(1.2f));
        c.g.DrawPath(&outline, &body);
        c.g.SetClip(&body);
        c.g.DrawLine(&outline, bx, splitY, bx + bw, splitY);
        c.g.DrawLine(&outline, cx, by, cx, splitY);
        c.g.ResetClip();
    }
    // wheel and side buttons
    auto part = [&](int n, Gdiplus::RectF box, float rad, const wchar_t* num, bool numLeft) {
        Look l = look(n, box);
        Gdiplus::GraphicsPath p;
        roundPath(p, box, rad);
        Gdiplus::SolidBrush base(gp(RGB(0x22, 0x26, 0x30)));
        c.g.FillPath(&base, &p);
        c.g.FillPath(l.brush.get(), &p);
        Gdiplus::Pen pen(gp(l.line), l.width);
        c.g.DrawPath(&pen, &p);
        RECT nr;
        if (numLeft)
            nr = {static_cast<LONG>(box.X - app.Sf(22) + c.ox), static_cast<LONG>(box.Y + c.oy),
                  static_cast<LONG>(box.X - app.Sf(4) + c.ox), static_cast<LONG>(box.Y + box.Height + c.oy)};
        else
            nr = {static_cast<LONG>(box.X - app.Sf(10) + c.ox), static_cast<LONG>(box.Y + box.Height + app.Sf(2) + c.oy),
                  static_cast<LONG>(box.X + box.Width + app.Sf(10) + c.ox),
                  static_cast<LONG>(box.Y + box.Height + app.Sf(16) + c.oy)};
        text(c, num, nr, app.fontTiny, phys[n] || script[n] ? Color::accentHover : l.number,
             (numLeft ? DT_RIGHT : DT_CENTER) | DT_VCENTER);
    };
    part(3, Gdiplus::RectF(cx - app.Sf(6), by + app.Sf(16), app.Sf(12), app.Sf(26)), app.Sf(6), L"3", false);
    part(5, Gdiplus::RectF(bx - app.Sf(7), by + bh * 0.46f, app.Sf(10), app.Sf(22)), app.Sf(4), L"5", true);
    part(4, Gdiplus::RectF(bx - app.Sf(7), by + bh * 0.46f + app.Sf(28), app.Sf(10), app.Sf(22)), app.Sf(4), L"4", true);

    // legend
    const RECT& lg = app.legendRect;
    if (!live) {
        text(c, L"Start a script to see buttons live", lg, app.fontSmall, Color::faint, DT_CENTER | DT_VCENTER);
        return;
    }
    const int dot = app.S(10), half = (lg.right - lg.left) / 2;
    RECT d1 = {lg.left + app.S(8), (lg.top + lg.bottom - dot) / 2, lg.left + app.S(8) + dot, (lg.top + lg.bottom + dot) / 2};
    gradientRound(c, d1, app.Sf(3), Color::accent, Color::accent2, 60);
    RECT t1 = {d1.right + app.S(6), lg.top, lg.left + half, lg.bottom};
    text(c, L"pressed", t1, app.fontSmall, Color::muted);
    RECT d2 = {lg.left + half + app.S(4), d1.top, lg.left + half + app.S(4) + dot, d1.bottom};
    fillRound(c, d2, app.Sf(3), mix(Color::field, Color::accent, 0.3));
    strokeRound(c, d2, app.Sf(3), Color::accentHover, app.Sf(1.5f));
    RECT t2 = {d2.right + app.S(6), lg.top, lg.right, lg.bottom};
    text(c, L"by script", t2, app.fontSmall, Color::muted);
}

void paintWindow(HDC target) {
    RECT client;
    GetClientRect(app.wnd, &client);
    ensureBackdrop();
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, client.right, client.bottom);
    HGDIOBJ old = SelectObject(dc, bmp);
    BitBlt(dc, 0, 0, client.right, client.bottom, app.backdropDC, 0, 0, SRCCOPY);
    {
        Canvas c(dc);

        // title bar
        const int ls = app.S(22);
        RECT logo = {app.S(14), (app.titleH - ls) / 2, app.S(14) + ls, (app.titleH + ls) / 2};
        drawLogo(c, logo);
        RECT tt = {logo.right + app.S(10), 0, logo.right + app.S(260), app.titleH};
        text(c, L"Logitech Script Bridge", tt, app.fontTitle, Color::text);
        const int tw = textWidth(dc, L"Logitech Script Bridge", app.fontTitle);
        RECT vt = {tt.left + tw + app.S(8), 0, tt.left + tw + app.S(60), app.titleH};
        text(c, kVersion, vt, app.fontSmall, Color::faint);

        // hero
        const RECT& hero = app.heroRect;
        const int pad = app.S(24);
        const wchar_t* status = app.status == App::Status::Running ? L"RUNNING"
                                : app.status == App::Status::Error ? L"SCRIPT ERROR" : L"STOPPED";
        const COLORREF sc = app.status == App::Status::Running ? Color::success
                            : app.status == App::Status::Error ? Color::danger : Color::muted;
        const int pw = textWidth(dc, status, app.fontCaption, 1) + app.S(30);
        RECT pill = {hero.left + pad, hero.top + app.S(20), hero.left + pad + pw, hero.top + app.S(44)};
        fillRound(c, pill, (pill.bottom - pill.top) / 2.0f, sc, 34);
        strokeRound(c, pill, (pill.bottom - pill.top) / 2.0f, sc, 1, 90);
        Gdiplus::SolidBrush sb(gp(sc));
        c.g.FillEllipse(&sb, pill.left + app.Sf(11), (pill.top + pill.bottom) / 2.0f - app.Sf(3.5f), app.Sf(7), app.Sf(7));
        RECT pt = {pill.left + app.S(23), pill.top, pill.right, pill.bottom};
        text(c, status, pt, app.fontCaption, mix(sc, Color::white, 0.25), DT_LEFT | DT_VCENTER, 1);

        RECT btn = clientRectOf(IDC_START);
        const bool hasScript = !app.scriptPath.empty();
        RECT name = {hero.left + pad, hero.top + app.S(50), btn.left - app.S(40), hero.top + app.S(88)};
        std::wstring scriptName = app.untitled ? L"Untitled script"
                                  : hasScript  ? widen(fileName(app.scriptPath))
                                               : L"No script selected";
        if (hasScript && scriptName.size() > 4 && _wcsicmp(scriptName.c_str() + scriptName.size() - 4, L".lua") == 0)
            scriptName = scriptName.substr(0, scriptName.size() - 4);
        text(c, scriptName, name, app.fontHero, hasScript || app.untitled ? Color::white : Color::muted);

        std::wstring sub;
        if (hasScript) {
            const std::string folder = fileName(dirName(app.scriptPath));
            sub = widen(folder.empty() ? app.scriptPath : folder + "\\" + fileName(app.scriptPath));
            sub += L"   ·   " + widen(kDevices[app.device].label);
            const double jmin = readPx(IDC_JITTER_MIN), jmax = readPx(IDC_JITTER_MAX);
            if (std::max(jmin, jmax) > 0)
                sub += L"   ·   wobble " + formatPx(std::min(jmin, jmax)) + L"–" + formatPx(std::max(jmin, jmax)) + L" px";
            if (kHotkeys[app.hotkey].vk) sub += L"   ·   " + std::wstring(kHotkeys[app.hotkey].label) + L" starts/stops";
        } else if (app.untitled) {
            sub = L"Not saved yet   ·   press Save (Ctrl+S) in the editor to keep it";
        } else {
            sub = L"Open a Logitech G HUB .lua script, write a new one, or drop one on this window";
        }
        if (hasScript && app.editorDirty) sub = L"Unsaved changes   ·   " + sub;
        RECT subr = {hero.left + pad, hero.top + app.S(88), btn.left - app.S(40), hero.top + app.S(108)};
        text(c, sub, subr, app.font, Color::muted);

        // pulsing ring around the big button while running
        if (app.running()) {
            const float t = (GetTickCount() % 1600) / 1600.0f;
            const float d = static_cast<float>(btn.right - btn.left) - app.Sf(4);
            const float grow = app.Sf(3) + t * app.Sf(16);
            Gdiplus::Pen ring(gp(Color::danger, static_cast<BYTE>(110 * (1 - t))), app.Sf(2));
            const float x = (btn.left + btn.right) / 2.0f - d / 2 - grow, y = (btn.top + btn.bottom) / 2.0f - d / 2 - grow;
            c.g.DrawEllipse(&ring, x, y, d + 2 * grow, d + 2 * grow);
        }
        const std::wstring big = app.running() ? L"Stop" : L"Start";
        const std::wstring keys = app.running() ? L"Shift+F5" : L"F5";
        const RECT& bl = app.bigLabelRect;
        const int bwid = textWidth(dc, big, app.fontBold), kwid = textWidth(dc, keys, app.fontSmall);
        const int bx = (bl.left + bl.right - bwid - kwid - app.S(8)) / 2;
        RECT b1 = {bx, bl.top, bx + bwid, bl.bottom};
        RECT b2 = {bx + bwid + app.S(8), bl.top, bx + bwid + app.S(8) + kwid, bl.bottom};
        text(c, big, b1, app.fontBold, Color::text);
        text(c, keys, b2, app.fontSmall, Color::faint);

        // card captions and labels
        for (const auto& card : app.cards) {
            RECT cr = {card.rect.left + app.S(18), card.rect.top + app.S(14), card.rect.right - app.S(18),
                       card.rect.top + app.S(30)};
            text(c, card.caption, cr, app.fontCaption, Color::muted, DT_LEFT | DT_VCENTER, 1);
        }
        if (!IsRectEmpty(&app.mouseCard)) {
            RECT lr = {app.mouseCard.left, app.mouseCard.top + app.S(14), app.mouseCard.right - app.S(18),
                       app.mouseCard.top + app.S(30)};
            text(c, app.running() ? L"● LIVE" : L"IDLE", lr, app.fontCaption,
                 app.running() ? Color::success : Color::faint, DT_RIGHT | DT_VCENTER, 1);
        }
        for (const auto& l : app.labels) text(c, l.text, l.rect, app.fontSmall, Color::muted);

        // device help callout
        if (!IsRectEmpty(&app.helpRect)) {
            RECT ib = {app.helpRect.left + app.S(12), app.helpRect.top + app.S(12), app.helpRect.left + app.S(28),
                       app.helpRect.top + app.S(28)};
            drawIcon(c, Icon::Info, ib, Color::accentHover, app.Sf(1.5f));
            RECT ht = {app.helpRect.left + app.S(38), app.helpRect.top + app.S(11), app.helpRect.right - app.S(12),
                       app.helpRect.bottom - app.S(8)};
            text(c, kDeviceHelp[app.device], ht, app.font, mix(Color::text, Color::muted, 0.35), DT_LEFT | DT_WORDBREAK);
        }

        // editor: line numbers and the live syntax check
        if (app.tab == 0) {
            HWND ed = editor();
            const RECT er = clientRectOf(IDC_EDITOR);
            const LONG first = static_cast<LONG>(SendMessageW(ed, EM_GETFIRSTVISIBLELINE, 0, 0));
            const LONG count = static_cast<LONG>(SendMessageW(ed, EM_GETLINECOUNT, 0, 0));
            CHARRANGE sel;
            SendMessageW(ed, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&sel));
            const LONG caret = static_cast<LONG>(SendMessageW(ed, EM_EXLINEFROMCHAR, 0, sel.cpMin));
            HRGN clip = CreateRectRgn(app.gutterRect.left, er.top, app.gutterRect.right, er.bottom);
            SelectClipRgn(dc, clip);
            for (LONG ln = first; ln < count; ln++) {
                const LONG ci = static_cast<LONG>(SendMessageW(ed, EM_LINEINDEX, ln, 0));
                if (ci < 0) break;
                POINTL pos = {};
                SendMessageW(ed, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&pos), ci);
                const int y = er.top + pos.y;
                if (y > er.bottom) break;
                RECT nr = {app.gutterRect.left, y, app.gutterRect.right - app.S(10), y + app.S(24)};
                const bool bad = ln + 1 == app.syntaxLine;
                text(c, std::to_wstring(ln + 1), nr, app.fontMono,
                     bad ? Color::danger : ln == caret ? Color::text : Color::faint, DT_RIGHT | DT_TOP);
                if (bad) {
                    Gdiplus::SolidBrush eb(gp(Color::danger));
                    c.g.FillEllipse(&eb, app.gutterRect.left + app.Sf(7), y + app.Sf(5), app.Sf(6), app.Sf(6));
                }
            }
            SelectClipRgn(dc, nullptr);
            DeleteObject(clip);

            std::wstring status;
            COLORREF sc = Color::faint;
            if (!app.syntaxError.empty()) {
                std::string msg = app.syntaxError;
                const size_t cut = msg.find(": ", msg.find(':') + 1);
                if (app.syntaxLine && cut != std::string::npos) msg = msg.substr(cut + 2);
                status = (app.syntaxLine ? L"Line " + std::to_wstring(app.syntaxLine) + L": " : L"") + widen(msg);
                sc = Color::danger;
            } else if (editorText(false).empty()) {
                status = L"Empty script";
            } else {
                status = L"No syntax errors";
                sc = mix(Color::success, Color::muted, 0.3);
            }
            RECT st = app.statusRect;
            if (app.syntaxError.empty() && !status.empty() && status != L"Empty script") {
                RECT ib = {st.left, (st.top + st.bottom) / 2 - app.S(7), st.left + app.S(14), (st.top + st.bottom) / 2 + app.S(7)};
                drawIcon(c, Icon::Check, ib, sc, app.Sf(1.8f));
                st.left += app.S(20);
            } else if (!app.syntaxError.empty()) {
                RECT ib = {st.left, (st.top + st.bottom) / 2 - app.S(7), st.left + app.S(14), (st.top + st.bottom) / 2 + app.S(7)};
                drawIcon(c, Icon::Info, ib, sc, app.Sf(1.5f));
                st.left += app.S(20);
            }
            text(c, status, st, app.fontSmall, sc);
        }

        paintMouse(c);
    }
    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
}

// ------------------------------------------------------------------ layout

void layout() {
    RECT rc;
    GetClientRect(app.wnd, &rc);
    const int W = rc.right, H = rc.bottom, m = app.S(20), gap = app.S(14), pad = app.S(18), fh = app.S(34),
              g8 = app.S(8);
    auto place = [](int id, int x, int y, int w, int h) { MoveWindow(app.item(id), x, y, w, h, FALSE); };
    auto field = [&](int id, int x, int y, int w, int rightInset = 0) {
        app.fields[id] = {x, y, x + w, y + fh};
        const int th = app.S(18);
        place(id, x + app.S(12), y + (fh - th) / 2, w - app.S(24) - rightInset, th);
    };
    app.cards.clear();
    app.labels.clear();
    app.fields.clear();

    // title bar
    app.titleH = app.S(44);
    const int cb = app.S(46);
    place(IDC_CLOSE, W - cb, 0, cb, app.titleH - 1);
    place(IDC_MAX, W - 2 * cb, 0, cb, app.titleH - 1);
    place(IDC_MIN, W - 3 * cb, 0, cb, app.titleH - 1);
    const int chipW = app.S(240);
    place(IDC_CONFIG, W - 3 * cb - app.S(16) - chipW, app.S(8), chipW, app.titleH - app.S(16));

    // hero
    app.heroRect = {m, app.titleH + app.S(8), W - m, app.titleH + app.S(8) + app.S(156)};
    const int d = app.S(88);
    RECT btn = {app.heroRect.right - app.S(52) - d, app.heroRect.top + app.S(20), app.heroRect.right - app.S(52),
                app.heroRect.top + app.S(20) + d};
    place(IDC_START, btn.left, btn.top, d, d);
    app.ringRect = btn;
    InflateRect(&app.ringRect, app.S(22), app.S(22));
    app.bigLabelRect = {btn.left - app.S(40), btn.bottom + app.S(8), btn.right + app.S(40), btn.bottom + app.S(28)};
    int x = app.heroRect.left + app.S(14);
    const int hy = app.heroRect.top + app.S(114);
    const struct { int id, width; } heroButtons[] = {{IDC_OPEN, 96}, {IDC_LIBRARY, 104}, {IDC_EDIT, 84}, {IDC_RELOAD, 96}};
    for (const auto& b : heroButtons) {
        place(b.id, x, hy, app.S(b.width), app.S(32));
        x += app.S(b.width) + app.S(4);
    }

    // device / options / mouse cards (hidden while the editor is expanded)
    const bool showCards = !app.expanded;
    for (int id : {IDC_DEVICE, IDC_PORT, IDC_PORT_MENU, IDC_BAUD, IDC_TEST, IDC_KEYFALLBACK, IDC_AUTOSTART, IDC_EXTRA,
                   IDC_HOTKEY, IDC_JITTER_MIN, IDC_JITTER_MAX})
        ShowWindow(app.item(id), showCards ? SW_SHOW : SW_HIDE);
    const int y = app.heroRect.bottom + gap, cardH = app.S(262);
    const int mouseW = app.S(230), avail = W - 2 * m - 2 * gap - mouseW;
    const int devW = avail * 45 / 100, optW = avail - devW;
    const RECT dev = {m, y, m + devW, y + cardH};
    const RECT opt = {dev.right + gap, y, dev.right + gap + optW, y + cardH};
    app.mouseCard = {opt.right + gap, y, W - m, y + cardH};
    app.cards.push_back({dev, L"OUTPUT DEVICE"});
    app.cards.push_back({opt, L"OPTIONS"});
    app.cards.push_back({app.mouseCard, L"MOUSE"});

    int inner = devW - 2 * pad;
    place(IDC_DEVICE, dev.left + pad, y + app.S(42), inner, fh);
    const int baudW = app.S(84), testW = app.S(76);
    const int portW = inner - baudW - testW - 2 * g8;
    app.labels.push_back({{dev.left + pad + app.S(2), y + app.S(86), dev.left + pad + portW, y + app.S(104)}, L"Port"});
    app.labels.push_back({{dev.left + pad + portW + g8 + app.S(2), y + app.S(86), dev.right, y + app.S(104)}, L"Baud"});
    field(IDC_PORT, dev.left + pad, y + app.S(106), portW, app.S(30));
    const RECT pf = app.fields[IDC_PORT];
    place(IDC_PORT_MENU, pf.right - app.S(34), pf.top + app.S(3), app.S(31), fh - app.S(6));
    field(IDC_BAUD, pf.right + g8, pf.top, baudW);
    place(IDC_TEST, pf.right + g8 + baudW + g8, pf.top, testW, fh);
    app.helpRect = {dev.left + pad, y + app.S(156), dev.right - pad, dev.bottom - pad};

    inner = optW - 2 * pad;
    const int labelW = app.S(140), cx = opt.left + pad + labelW, cw = inner - labelW;
    auto row = [&](int i) { return y + app.S(40) + i * app.S(42); };
    place(IDC_KEYFALLBACK, opt.left + pad, row(0) + app.S(2), inner, app.S(30));
    place(IDC_AUTOSTART, opt.left + pad, row(1) + app.S(2), inner, app.S(30));
    app.labels.push_back({{opt.left + pad + app.S(2), row(2), cx - g8, row(2) + fh}, L"F13–F24 keys"});
    place(IDC_EXTRA, cx, row(2), cw, fh);
    app.labels.push_back({{opt.left + pad + app.S(2), row(3), cx - g8, row(3) + fh}, L"Start/stop hotkey"});
    place(IDC_HOTKEY, cx, row(3), cw, fh);
    const int jl = app.S(34), jw = (cw - 2 * jl - g8) / 2;
    app.labels.push_back({{opt.left + pad + app.S(2), row(4), cx - g8, row(4) + fh}, L"Randomize (px)"});
    app.labels.push_back({{cx, row(4), cx + jl, row(4) + fh}, L"Min"});
    field(IDC_JITTER_MIN, cx + jl, row(4), jw);
    app.labels.push_back({{cx + jl + jw + g8, row(4), cx + 2 * jl + jw + g8, row(4) + fh}, L"Max"});
    field(IDC_JITTER_MAX, cx + 2 * jl + jw + g8, row(4), jw);

    app.mouseArea = {app.mouseCard.left + app.S(12), y + app.S(34), app.mouseCard.right - app.S(12),
                     app.mouseCard.bottom - app.S(36)};
    app.legendRect = {app.mouseCard.left + app.S(12), app.mouseCard.bottom - app.S(38), app.mouseCard.right - app.S(12),
                      app.mouseCard.bottom - app.S(14)};

    if (!showCards) {
        app.cards.clear();
        app.labels.clear();
        app.fields.clear();
        app.helpRect = app.mouseCard = app.mouseArea = app.legendRect = RECT{};
    }

    // script editor / log, sharing one card with tabs
    const int ly = showCards ? y + cardH + gap : app.heroRect.bottom + gap;
    app.cards.push_back({{m, ly, W - m, H - m}, L""});
    app.tabsRect = {m + pad - app.S(4), ly + app.S(10), m + pad - app.S(4) + app.S(176), ly + app.S(42)};
    place(IDC_TAB_SCRIPT, app.tabsRect.left + app.S(3), app.tabsRect.top + app.S(3), app.S(84), app.S(26));
    place(IDC_TAB_LOG, app.tabsRect.left + app.S(89), app.tabsRect.top + app.S(3), app.S(84), app.S(26));
    const bool script = app.tab == 0;
    int rx = W - m - pad + app.S(4);
    auto header = [&](int id, int width, bool visible) {
        ShowWindow(app.item(id), visible ? SW_SHOW : SW_HIDE);
        if (!visible) return;
        rx -= app.S(width);
        place(id, rx, ly + app.S(10), app.S(width), app.S(32));
        rx -= app.S(4);
    };
    header(IDC_EXPAND, 34, true);
    header(IDC_SAVESCRIPT, 86, script);
    header(IDC_REVERT, 92, script);
    header(IDC_NEW, 76, script);
    header(IDC_CLEAR, 84, !script);
    app.statusRect = script ? RECT{app.tabsRect.right + app.S(16), ly + app.S(10), rx - app.S(8), ly + app.S(42)} : RECT{};

    const RECT box = {m + pad, ly + app.S(52), W - m - pad, H - m - pad};
    ShowWindow(app.item(IDC_EDITOR), script ? SW_SHOW : SW_HIDE);
    ShowWindow(app.item(IDC_LOG), script ? SW_HIDE : SW_SHOW);
    if (script) {
        app.fields[IDC_EDITOR] = box;
        app.gutterRect = {box.left, box.top, box.left + app.S(50), box.bottom};
        place(IDC_EDITOR, app.gutterRect.right + app.S(10), box.top + app.S(8), box.right - app.gutterRect.right - app.S(14),
              box.bottom - box.top - app.S(12));
    } else {
        app.fields[IDC_LOG] = box;
        app.gutterRect = RECT{};
        place(IDC_LOG, box.left + app.S(10), box.top + app.S(8), box.right - box.left - app.S(14),
              box.bottom - box.top - app.S(14));
    }
    app.gutterFirst = app.gutterCaret = -1;

    // controls were moved without repainting; redraw everything at the new size
    redrawAll();
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
    } else if (msg == WM_ERASEBKGND) {
        return 1;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

HWND make(const wchar_t* cls, const wchar_t* label, DWORD style, int id, HFONT font = nullptr) {
    HWND h = CreateWindowExW(0, cls, label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, 0, 0, 10, 10, app.wnd,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : app.font), TRUE);
    return h;
}

void button(int id, const wchar_t* label, Role role, Icon icon = Icon::None) {
    HWND h = make(L"BUTTON", label, BS_OWNERDRAW, id);
    app.buttons[id] = {role, icon};
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
    BOOL on = TRUE;
    if (FAILED(DwmSetWindowAttribute(app.wnd, 20, &on, sizeof(on)))) DwmSetWindowAttribute(app.wnd, 19, &on, sizeof(on));
    const int corner = 2;  // DWMWCP_ROUND on Windows 11
    DwmSetWindowAttribute(app.wnd, 33, &corner, sizeof(corner));
    // dark scrollbars and tooltips where Windows supports it
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
    SetWindowTheme(app.item(IDC_LOG), L"DarkMode_Explorer", nullptr);
    SetWindowTheme(app.item(IDC_EDITOR), L"DarkMode_Explorer", nullptr);
}

void createControls() {
    const wchar_t* ui = fontExists(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
    const wchar_t* display = fontExists(L"Segoe UI Variable Display") ? L"Segoe UI Variable Display" : L"Segoe UI";
    app.font = makeFont(14, FW_NORMAL, ui);
    app.fontBold = makeFont(14, FW_SEMIBOLD, ui);
    app.fontSmall = makeFont(12, FW_SEMIBOLD, ui);
    app.fontCaption = makeFont(11, FW_BOLD, ui);
    app.fontTiny = makeFont(10, FW_BOLD, ui);
    app.fontTitle = makeFont(14, FW_SEMIBOLD, ui);
    app.fontHero = makeFont(28, FW_SEMIBOLD, display);
    app.monoFace = fontExists(L"Cascadia Mono") ? L"Cascadia Mono" : L"Consolas";
    app.fontMono = makeFont(13, FW_NORMAL, app.monoFace.c_str());
    app.fieldBrush = CreateSolidBrush(Color::field);

    button(IDC_MIN, L"", Role::Caption, Icon::Minimize);
    button(IDC_MAX, L"", Role::Caption, Icon::Maximize);
    button(IDC_CLOSE, L"", Role::CaptionClose, Icon::Close);
    button(IDC_CONFIG, L"", Role::Chip);
    button(IDC_START, L"", Role::Hero);
    button(IDC_OPEN, L"Open", Role::Ghost, Icon::Folder);
    button(IDC_LIBRARY, L"Scripts", Role::Ghost, Icon::List);
    button(IDC_EDIT, L"Edit", Role::Ghost, Icon::Edit);
    button(IDC_RELOAD, L"Reload", Role::Ghost, Icon::Reload);
    button(IDC_DEVICE, L"", Role::Dropdown);
    make(L"EDIT", L"", ES_AUTOHSCROLL, IDC_PORT);
    button(IDC_PORT_MENU, L"", Role::Chevron);
    make(L"EDIT", L"", ES_NUMBER | ES_AUTOHSCROLL, IDC_BAUD);
    button(IDC_TEST, L"Test", Role::Secondary, Icon::Pointer);
    button(IDC_KEYFALLBACK, L"Type keys in software when the device can't", Role::Toggle);
    button(IDC_AUTOSTART, L"Start the script when the app opens", Role::Toggle);
    button(IDC_EXTRA, L"", Role::Dropdown);
    button(IDC_HOTKEY, L"", Role::Dropdown);
    make(L"EDIT", L"0", ES_AUTOHSCROLL, IDC_JITTER_MIN);
    make(L"EDIT", L"0", ES_AUTOHSCROLL, IDC_JITTER_MAX);
    button(IDC_CLEAR, L"Clear", Role::Ghost, Icon::Trash);
    button(IDC_TAB_SCRIPT, L"Script", Role::Tab);
    button(IDC_TAB_LOG, L"Log", Role::Tab);
    button(IDC_NEW, L"New", Role::Ghost, Icon::Plus);
    button(IDC_REVERT, L"Revert", Role::Ghost, Icon::Reload);
    button(IDC_SAVESCRIPT, L"Save", Role::Secondary, Icon::Save);
    button(IDC_EXPAND, L"", Role::Ghost, Icon::Maximize);

    // script editor (rich edit with Lua colouring)
    LoadLibraryW(L"Msftedit.dll");
    HWND ed = make(MSFTEDIT_CLASS, L"",
                   WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOHIDESEL | ES_WANTRETURN,
                   IDC_EDITOR, nullptr);
    SendMessageW(ed, EM_EXLIMITTEXT, 0, 4 * 1024 * 1024);
    SendMessageW(ed, EM_SETTARGETDEVICE, 0, 1);  // no word wrap: one line of code per line
    SendMessageW(ed, EM_SETBKGNDCOLOR, 0, Color::logBg);
    SendMessageW(ed, EM_SETUNDOLIMIT, 500, 0);
    {
        CHARFORMAT2W ef = {};
        ef.cbSize = sizeof(ef);
        ef.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE | CFM_CHARSET | CFM_ITALIC | CFM_BOLD;
        ef.crTextColor = tokColor(Tok::Default);
        ef.yHeight = 13 * 72 * 20 / 96;
        ef.bCharSet = DEFAULT_CHARSET;
        lstrcpynW(ef.szFaceName, app.monoFace.c_str(), LF_FACESIZE);
        SendMessageW(ed, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&ef));
    }
    SendMessageW(ed, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_UPDATE);
    SetWindowSubclass(ed, editorProc, 0, 0);
    {
        // text object model: lets colouring skip the undo history
        const GUID iidTextDocument = {0x8CC497C0, 0xA1DF, 0x11CE, {0x80, 0x98, 0x00, 0xAA, 0x00, 0x47, 0xBE, 0x5D}};
        IUnknown* unk = nullptr;
        if (SendMessageW(ed, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&unk)) && unk) {
            unk->QueryInterface(iidTextDocument, reinterpret_cast<void**>(&app.editorDoc));
            unk->Release();
        }
    }

    // colour-coded log (rich edit)
    HWND log = make(MSFTEDIT_CLASS, L"", WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, IDC_LOG, app.fontMono);
    SendMessageW(log, EM_EXLIMITTEXT, 0, 8 * 1024 * 1024);
    SendMessageW(log, EM_SETBKGNDCOLOR, 0, Color::logBg);
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE | CFM_CHARSET;
    cf.crTextColor = logColor(LogKind::Script);
    cf.yHeight = 13 * 72 * 20 / 96;  // 13 px in twips
    cf.bCharSet = DEFAULT_CHARSET;
    lstrcpynW(cf.szFaceName, app.monoFace.c_str(), LF_FACESIZE);
    SendMessageW(log, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));

    for (int id : {IDC_PORT, IDC_BAUD, IDC_JITTER_MIN, IDC_JITTER_MAX})
        SendMessageW(app.item(id), EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));
    for (int id : {IDC_JITTER_MIN, IDC_JITTER_MAX}) SendMessageW(app.item(id), EM_SETLIMITTEXT, 6, 0);
    SendMessageW(log, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(app.S(4), app.S(4)));
    SendMessageW(app.item(IDC_PORT), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"e.g. COM5"));

    // tooltips with the keyboard shortcuts
    app.tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0,
                                  0, 0, app.wnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowTheme(app.tooltip, L"DarkMode_Explorer", nullptr);
    SendMessageW(app.tooltip, TTM_SETMAXTIPWIDTH, 0, app.S(360));
    const struct { int id; const wchar_t* tip; } tips[] = {
        {IDC_START, L"Start the script (F5) / stop it (Shift+F5)"},
        {IDC_OPEN, L"Open a .lua script (Ctrl+O)"},
        {IDC_LIBRARY, L"Scripts in the scripts\\ and examples\\ folders next to the app"},
        {IDC_EDIT, L"Edit the script right here (Ctrl+E)"},
        {IDC_TAB_SCRIPT, L"Edit the script (Ctrl+E)"},
        {IDC_TAB_LOG, L"What the script printed, plus app messages"},
        {IDC_NEW, L"Start a new script from a template"},
        {IDC_REVERT, L"Throw away unsaved changes"},
        {IDC_SAVESCRIPT, L"Save the script (Ctrl+S while editing). A running script restarts with the new code"},
        {IDC_EXPAND, L"More room for the editor (hides the device and options cards)"},
        {IDC_RELOAD, L"Restart the script after editing it (Ctrl+R)"},
        {IDC_CONFIG, L"Save, save as or load a config"},
        {IDC_PORT_MENU, L"Serial ports that are plugged in right now"},
        {IDC_TEST, L"Wiggle the pointer through the device to check that it works"},
        {IDC_HOTKEY, L"Starts and stops the script from anywhere, even while another app is in front"},
        {IDC_EXTRA, L"Bind your mouse's extra buttons to F13–F24 in its own software, then pick what they do here"},
        {IDC_JITTER_MIN, L"Each mouse movement lands between Min and Max pixels off the exact path (decimals allowed, 0 = off)"},
        {IDC_JITTER_MAX, L"Each mouse movement lands between Min and Max pixels off the exact path (decimals allowed, 0 = off)"},
        {IDC_CLEAR, L"Clear the log"},
        {IDC_MIN, L"Minimize"},
        {IDC_MAX, L"Maximize"},
        {IDC_CLOSE, L"Close"},
    };
    for (const auto& t : tips) {
        TTTOOLINFOW ti = {};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd = app.wnd;
        ti.uId = reinterpret_cast<UINT_PTR>(app.item(t.id));
        ti.lpszText = const_cast<wchar_t*>(t.tip);
        SendMessageW(app.tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
    }

    ACCEL keys[] = {
        {FCONTROL | FVIRTKEY, 'O', IDC_OPEN},    {FCONTROL | FVIRTKEY, 'L', IDC_LOADCFG},
        {FCONTROL | FVIRTKEY, 'S', IDC_SAVECFG}, {FCONTROL | FSHIFT | FVIRTKEY, 'S', IDC_SAVEAS},
        {FVIRTKEY, VK_F5, IDC_START},            {FSHIFT | FVIRTKEY, VK_F5, IDC_STOP},
        {FCONTROL | FVIRTKEY, 'R', IDC_RELOAD},  {FCONTROL | FVIRTKEY, 'E', IDC_EDIT},
    };
    app.accel = CreateAcceleratorTableW(keys, sizeof(keys) / sizeof(keys[0]));
}

void onCommand(int id, int code) {
    // shortcuts arrive for disabled buttons too
    HWND ctl = app.item(id);
    if (code == 1 && ctl && !IsWindowEnabled(ctl)) return;
    switch (id) {
        case IDC_START:
            if (app.running()) {
                if (code != 1) stopScript();  // F5 only starts; the button toggles
            } else {
                startScript();
            }
            break;
        case IDC_STOP:
            if (app.running()) stopScript();
            break;
        case IDC_RELOAD:
            if (app.running()) {
                stopScript();
                startScript();
            }
            break;
        case IDC_OPEN: openScript(); break;
        case IDC_LIBRARY: libraryMenu(); break;
        case IDC_LOADCFG:
            if (!app.running()) loadConfigDialog();
            break;
        case IDC_SAVECFG:
            // Ctrl+S saves the script while you're typing in it, otherwise the config
            if (GetFocus() == editor()) saveEditor(true);
            else saveConfigFile();
            break;
        case IDC_SAVEAS: saveConfigAs(); break;
        case IDC_CONFIG: configMenu(); break;
        case IDC_EDIT: showTab(0); break;
        case IDC_TAB_SCRIPT: showTab(0); break;
        case IDC_TAB_LOG: showTab(1); break;
        case IDC_NEW: newScript(); break;
        case IDC_REVERT: revertScript(); break;
        case IDC_SAVESCRIPT: saveEditor(true); break;
        case IDC_EXPAND:
            app.expanded = !app.expanded;
            app.buttons[IDC_EXPAND].icon = app.expanded ? Icon::Restore : Icon::Maximize;
            layout();
            break;
        case IDC_EDITOR:
            if (code == EN_CHANGE && !app.loadingEditor && !app.highlighting) {
                SetTimer(app.wnd, kEditTimer, 250, nullptr);  // recolour + re-check once typing pauses
                InvalidateRect(app.wnd, &app.gutterRect, FALSE);
            }
            if (code == EN_UPDATE) checkGutter();
            break;
        case IDC_PORT_MENU: portMenu(); break;
        case IDC_DEVICE: deviceMenu(); break;
        case IDC_EXTRA: extraMenu(); break;
        case IDC_HOTKEY: hotkeyMenu(); break;
        case IDC_TEST: testDevice(); break;
        case IDC_CLEAR: clearLog(); break;
        case IDC_KEYFALLBACK: toggle(id, app.keyFallback); break;
        case IDC_AUTOSTART: toggle(id, app.autoStart); break;
        case IDC_MIN: ShowWindow(app.wnd, SW_MINIMIZE); break;
        case IDC_MAX: ShowWindow(app.wnd, IsZoomed(app.wnd) ? SW_RESTORE : SW_MAXIMIZE); break;
        case IDC_CLOSE: PostMessageW(app.wnd, WM_CLOSE, 0, 0); break;
        case IDC_PORT:
        case IDC_BAUD:
        case IDC_JITTER_MIN:
        case IDC_JITTER_MAX:
            if (code == EN_CHANGE) refreshChrome();
            if (code == EN_SETFOCUS || code == EN_KILLFOCUS) {
                app.backdropDirty = true;  // the focus ring is part of the backdrop
                RECT r = app.fields[id];
                InflateRect(&r, app.S(2), app.S(2));
                RedrawWindow(app.wnd, &r, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
            }
            break;
    }
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCCALCSIZE:
            // draw our own title bar: drop the caption, keep the resize frame on the other sides
            if (wp) {
                auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
                const int fx = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                const int fy = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                p->rgrc[0].left += fx;
                p->rgrc[0].right -= fx;
                p->rgrc[0].bottom -= fy;
                if (IsZoomed(hwnd)) p->rgrc[0].top += fy;
                return 0;
            }
            break;
        case WM_NCHITTEST: {
            const LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
            if (hit != HTCLIENT) return hit;
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            if (!IsZoomed(hwnd) && pt.y < app.S(5)) return HTTOP;
            if (pt.y < app.titleH) return HTCAPTION;
            return HTCLIENT;
        }
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
            mm->ptMinTrackSize = {app.S(1000), app.S(690)};
            return 0;
        }
        case WM_DRAWITEM:
            drawButton(reinterpret_cast<const DRAWITEMSTRUCT*>(lp));
            return TRUE;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, IsWindowEnabled(reinterpret_cast<HWND>(lp)) ? Color::text : Color::faint);
            SetBkColor(dc, Color::field);
            return reinterpret_cast<LRESULT>(app.fieldBrush);
        }
        case WM_COMMAND:
            onCommand(LOWORD(wp), HIWORD(wp));
            return 0;
        case WM_DROPFILES: {
            wchar_t file[MAX_PATH];
            if (DragQueryFileW(reinterpret_cast<HDROP>(wp), 0, file, MAX_PATH) && !app.running()) {
                const std::string path = narrow(file);
                if (path.size() > 4 && _stricmp(path.c_str() + path.size() - 4, ".ini") == 0) loadConfigFile(path);
                else setScript(path);
            }
            DragFinish(reinterpret_cast<HDROP>(wp));
            return 0;
        }
        case WM_TIMER:
            if (wp == kLogTimer) {
                flushLog();
                unsigned bits = app.engine ? 0u : 0x400u;  // repaint the mouse only when something changed
                for (int i = 0; i < 5 && app.engine; i++) {
                    bool phys, script;
                    app.engine->buttonState(i + 1, phys, script);
                    bits |= (phys ? 1u : 0u) << i | (script ? 1u : 0u) << (i + 5);
                }
                if (bits != app.lastButtons) {
                    app.lastButtons = bits;
                    InvalidateRect(hwnd, &app.mouseCard, FALSE);
                }
            } else if (wp == kEditTimer) {
                KillTimer(hwnd, kEditTimer);
                highlightEditor();
                updateEditorState();
                InvalidateRect(hwnd, &app.gutterRect, FALSE);
            } else if (wp == kAnimTimer) {
                if (app.running()) InvalidateRect(hwnd, &app.ringRect, FALSE);
                for (auto it = app.anims.begin(); it != app.anims.end();) {
                    InvalidateRect(app.item(it->first), nullptr, FALSE);
                    if (GetTickCount() - it->second.start > 200) it = app.anims.erase(it);
                    else ++it;
                }
            }
            return 0;
        case WM_HOTKEY:
            if (wp == kHotkeyId) {
                if (app.running()) stopScript();
                else startScript();
            }
            return 0;
        case WM_APP_CLEARLOG:
            clearLog();
            return 0;
        case WM_CLOSE:
            if (!confirmDiscardEdits()) return 0;
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

std::string savedWindow;

// Puts the window back where it was last time, if that spot is still on a screen.
void restoreWindow(int show) {
    int x, y, w, h;
    char max[4] = "";
    if (std::sscanf(savedWindow.c_str(), "%d,%d,%d,%d,%3s", &x, &y, &w, &h, max) >= 4 && w > 0 && h > 0) {
        RECT r = {x, y, x + w, y + h};
        if (MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) {
            WINDOWPLACEMENT wp = {sizeof(wp)};
            wp.rcNormalPosition = r;
            wp.showCmd = std::strcmp(max, "max") == 0 ? SW_SHOWMAXIMIZED : show;
            SetWindowPlacement(app.wnd, &wp);
            return;
        }
    }
    ShowWindow(app.wnd, show);
}

void loadState(const std::wstring& arg) {
    Config c;
    ConfigExtras extras;
    std::string err;
    loadConfig(app.statePath, c, err, &extras);
    savedWindow = extras["window"];
    app.configPath = extras["config_file"].empty() ? "" : normalizePath(joinPath(app.exeDir, extras["config_file"]));
    if (!app.configPath.empty() && !loadConfig(app.configPath, app.savedConfig, err)) app.configPath.clear();
    applyConfig(c);
    if (!arg.empty()) {
        const std::string a = narrow(arg);
        if (a.size() > 4 && _stricmp(a.c_str() + a.size() - 4, ".ini") == 0) loadConfigFile(a);
        else setScript(a);
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

    // default size, shrunk to fit small screens
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int winW = std::min<int>(app.S(1080), work.right - work.left);
    const int winH = std::min<int>(app.S(860), work.bottom - work.top);
    app.wnd = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"Logitech Script Bridge",
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, work.left + (work.right - work.left - winW) / 2,
                              work.top + (work.bottom - work.top - winH) / 2, winW, winH, nullptr, nullptr, inst, nullptr);
    SetWindowPos(app.wnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    createControls();
    enableDarkChrome();
    loadState(arg);
    layout();
    refreshChrome();
    restoreWindow(show);
    DragAcceptFiles(app.wnd, TRUE);
    SetTimer(app.wnd, kLogTimer, 50, nullptr);
    SetTimer(app.wnd, kAnimTimer, 30, nullptr);
    app.log("Pick a script, choose your device and press Start (F5). Hover over a button to see its shortcut.\n");
    if (app.autoStart && !app.scriptPath.empty()) PostMessageW(app.wnd, WM_COMMAND, IDC_START, 0);

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
