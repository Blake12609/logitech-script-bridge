#include "platform_win.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <future>

// ------------------------------------------------------------------ mouse hook

namespace {

std::atomic<Engine*> g_engine{nullptr};
std::atomic<int> g_extraKeys{0};  // 0 off, 1 mouse buttons 6-17, 2 G-keys
bool g_extraDown[12] = {};        // hook thread only

// F13-F24 are the keys mouse software can usually assign to extra buttons.
LRESULT CALLBACK keyboardProc(int code, WPARAM msg, LPARAM lp) {
    Engine* e = g_engine.load();
    const int mode = g_extraKeys.load();
    if (code == HC_ACTION && e && mode) {
        const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
        if (k->vkCode >= VK_F13 && k->vkCode <= VK_F24 && !(k->flags & LLKHF_INJECTED)) {
            const int i = static_cast<int>(k->vkCode - VK_F13);
            const bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
            if (down != g_extraDown[i]) {  // skip auto-repeat
                g_extraDown[i] = down;
                if (mode == 1) e->onExtraButton(6 + i, down);
                else e->onGKey(1 + i, down);
            }
            return 1;  // swallow it: the key now belongs to the script
        }
    }
    return CallNextHookEx(nullptr, code, msg, lp);
}

LRESULT CALLBACK mouseProc(int code, WPARAM msg, LPARAM lp) {
    Engine* e = g_engine.load();
    if (code == HC_ACTION && e) {
        const auto* m = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
        switch (msg) {
            case WM_LBUTTONDOWN: e->onPhysicalButton(Button::Left, true); break;
            case WM_LBUTTONUP: e->onPhysicalButton(Button::Left, false); break;
            case WM_RBUTTONDOWN: e->onPhysicalButton(Button::Right, true); break;
            case WM_RBUTTONUP: e->onPhysicalButton(Button::Right, false); break;
            case WM_MBUTTONDOWN: e->onPhysicalButton(Button::Middle, true); break;
            case WM_MBUTTONUP: e->onPhysicalButton(Button::Middle, false); break;
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
                e->onPhysicalButton(HIWORD(m->mouseData) == XBUTTON1 ? Button::Side1 : Button::Side2,
                                    msg == WM_XBUTTONDOWN);
                break;
        }
    }
    return CallNextHookEx(nullptr, code, msg, lp);
}

}  // namespace

bool WinInput::startHook(Engine* engine, const std::string& extraKeys, std::string& error) {
    stopHook();
    g_engine = engine;
    g_extraKeys = extraKeys == "mouse" ? 1 : extraKeys == "gkeys" ? 2 : 0;
    for (bool& d : g_extraDown) d = false;
    std::promise<DWORD> ready;  // 0 = hook installed, else error code
    auto started = ready.get_future();
    thread_ = std::thread([this, &ready] {
        MSG msg;
        PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);  // create the message queue
        HHOOK hook = SetWindowsHookExW(WH_MOUSE_LL, mouseProc, GetModuleHandleW(nullptr), 0);
        HHOOK kbHook = g_extraKeys ? SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, GetModuleHandleW(nullptr), 0)
                                   : nullptr;
        threadId_ = GetCurrentThreadId();
        const bool ok = hook && (kbHook || !g_extraKeys);
        ready.set_value(ok ? 0 : (GetLastError() ? GetLastError() : 1));
        if (ok) {
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            }
        }
        if (hook) UnhookWindowsHookEx(hook);
        if (kbHook) UnhookWindowsHookEx(kbHook);
    });
    if (DWORD code = started.get()) {
        error = "Could not install the input hooks (error " + std::to_string(code) + ").";
        thread_.join();
        g_engine = nullptr;
        return false;
    }
    return true;
}

void WinInput::stopHook() {
    g_engine = nullptr;
    if (thread_.joinable()) {
        PostThreadMessageW(threadId_, WM_QUIT, 0, 0);
        thread_.join();
    }
}

// ------------------------------------------------------------------ state queries

bool WinInput::modifierPressed(const std::string& name) {
    int vk = 0;
    if (name == "lshift") vk = VK_LSHIFT;
    else if (name == "rshift") vk = VK_RSHIFT;
    else if (name == "lctrl") vk = VK_LCONTROL;
    else if (name == "rctrl") vk = VK_RCONTROL;
    else if (name == "lalt") vk = VK_LMENU;
    else if (name == "ralt") vk = VK_RMENU;
    return vk && (GetAsyncKeyState(vk) & 0x8000);
}

bool WinInput::lockOn(const std::string& name) {
    int vk = 0;
    if (name == "capslock") vk = VK_CAPITAL;
    else if (name == "numlock") vk = VK_NUMLOCK;
    else if (name == "scrolllock") vk = VK_SCROLL;
    return vk && (GetKeyState(vk) & 1);
}

void WinInput::cursorPos(int& x, int& y) {
    POINT p = {0, 0};
    GetCursorPos(&p);
    x = p.x;
    y = p.y;
}

void WinInput::screenRect(bool virtualDesktop, int& left, int& top, int& width, int& height) {
    if (virtualDesktop) {
        left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    } else {
        left = top = 0;
        width = GetSystemMetrics(SM_CXSCREEN);
        height = GetSystemMetrics(SM_CYSCREEN);
    }
}

// ------------------------------------------------------------------ software output

namespace {

class SendInputBackend : public Backend {
public:
    bool supportsKeyboard() const override { return true; }

    void move(int dx, int dy) override {
        INPUT in = {};
        in.type = INPUT_MOUSE;
        in.mi.dx = dx;
        in.mi.dy = dy;
        in.mi.dwFlags = MOUSEEVENTF_MOVE;
        SendInput(1, &in, sizeof(in));
    }

    void wheel(int clicks) override {
        INPUT in = {};
        in.type = INPUT_MOUSE;
        in.mi.mouseData = static_cast<DWORD>(clicks * WHEEL_DELTA);
        in.mi.dwFlags = MOUSEEVENTF_WHEEL;
        SendInput(1, &in, sizeof(in));
    }

    void button(Button b, bool down) override {
        INPUT in = {};
        in.type = INPUT_MOUSE;
        switch (b) {
            case Button::Left: in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP; break;
            case Button::Right: in.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP; break;
            case Button::Middle: in.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
            case Button::Side1:
            case Button::Side2:
                in.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
                in.mi.mouseData = b == Button::Side1 ? XBUTTON1 : XBUTTON2;
                break;
        }
        SendInput(1, &in, sizeof(in));
    }

    void key(const KeyInfo& k, bool down) override {
        INPUT in = {};
        in.type = INPUT_KEYBOARD;
        const std::string name = k.name;
        if (name == "pause" || name == "numlock") {
            // these two don't map cleanly to a single scancode
            in.ki.wVk = static_cast<WORD>(name == "pause" ? VK_PAUSE : VK_NUMLOCK);
            in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
        } else {
            in.ki.wScan = static_cast<WORD>(k.scancode & 0xFF);
            in.ki.dwFlags = KEYEVENTF_SCANCODE | (k.scancode > 0xFF ? KEYEVENTF_EXTENDEDKEY : 0) |
                            (down ? 0 : KEYEVENTF_KEYUP);
        }
        SendInput(1, &in, sizeof(in));
    }
};

}  // namespace

std::unique_ptr<Backend> createSoftwareBackend() { return std::make_unique<SendInputBackend>(); }
