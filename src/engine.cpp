#include "engine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

// Lua is compiled as C++ (see CMakeLists.txt) so script errors unwind as
// exceptions and C++ destructors run normally.
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

namespace {

const char* const kStopMarker = "__LSB_SCRIPT_STOPPED__";
const auto kEchoWindow = std::chrono::milliseconds(500);
const int kEventButton[kButtonCount] = {1, 2, 3, 4, 5};  // OnEvent numbering: L R M back fwd

// PressMouseButton / IsMouseButtonPressed numbering (differs from OnEvent's!)
bool outputButton(lua_Integer n, Button& b) {
    switch (n) {
        case 1: b = Button::Left; return true;
        case 2: b = Button::Middle; return true;
        case 3: b = Button::Right; return true;
        case 4: b = Button::Side1; return true;
        case 5: b = Button::Side2; return true;
        default: return false;
    }
}

int toInt(lua_State* L, int idx) { return static_cast<int>(std::lround(luaL_checknumber(L, idx))); }

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

const char* const kPrelude = R"LUA(
local api = ...

-- keep only what G Hub scripts are allowed to use
io, dofile, loadfile, require, package, debug = nil, nil, nil, nil, nil, nil
os = { time = os.time, clock = os.clock, date = os.date, difftime = os.difftime }
local text_load = load
load = function(chunk, name, mode, env) return text_load(chunk, name, "t", env) end
loadstring = load
unpack = unpack or table.unpack

local fmt = string.format
function OutputLogMessage(...) api.log(fmt(...)) end
function OutputDebugMessage(...) api.log(fmt(...)) end
function ClearLog() api.clear_log() end
function OutputLCDMessage(text, timeout) end
function ClearLCD() end
function Sleep(ms) api.sleep(ms or 0) end
function GetRunningTime() return api.running_time() end
function GetDate(f, t) return os.date(f, t) end

function PressKey(...) api.key(true, ...) end
function ReleaseKey(...) api.key(false, ...) end
function PressAndReleaseKey(...) api.key(true, ...); api.key(false, ...) end
function IsModifierPressed(name) return api.modifier(name) end
function IsKeyLockOn(name) return api.lock(name) end

function PressMouseButton(b) api.mbutton(b, true) end
function ReleaseMouseButton(b) api.mbutton(b, false) end
function PressAndReleaseMouseButton(b) api.mbutton(b, true); api.mbutton(b, false) end
function IsMouseButtonPressed(b) return api.mpressed(b) end
function MoveMouseRelative(x, y) api.move_rel(x, y) end
function MoveMouseWheel(n) api.wheel(n) end
function MoveMouseTo(x, y) api.move_to(x, y, false) end
function MoveMouseToVirtual(x, y) api.move_to(x, y, true) end
function GetMousePosition() return api.mouse_pos() end
function EnablePrimaryMouseButtonEvents(on) api.primary(on and true or false) end

local mstate = { kb = 1, lhc = 1, mouse = 1 }
function GetMKeyState(family) return mstate[family or "kb"] or 1 end
function SetMKeyState(m, family) mstate[family or "kb"] = m end

-- lighting / macro / DPI functions have nothing to drive here
function PlayMacro(name) api.log("PlayMacro(" .. tostring(name) .. ") is not supported\n") end
function PressMacro(name) PlayMacro(name) end
function ReleaseMacro(name) end
function AbortMacro() end
function SetBacklightColor() end
function SetMouseDPITable() end
function SetMouseDPITableIndex() end
function EnableHidEvents() end
function SetSteeringWheelProperty() end
)LUA";

}  // namespace

Engine::Engine(Backend& out, InputState& input, LogFn log, Backend* keyFallback)
    : out_(out), input_(input), log_(std::move(log)), keyFallback_(keyFallback) {}

Engine::~Engine() { stop(); }

// ------------------------------------------------------------------ lifecycle

bool Engine::start(const std::string& source, const std::string& chunkName, std::string& error) {
    if (thread_.joinable()) stop();
    {
        std::lock_guard<std::mutex> lock(qmu_);
        queue_.clear();
    }
    abort_ = false;
    hasDeadline_ = false;
    primaryEvents_ = false;
    warnedKeyboard_ = false;
    offX_ = offY_ = 0;
    start_ = Clock::now();
    physical_.fill(false);
    synthetic_.fill(false);
    extra_.fill(false);
    for (auto& e : echo_) e.clear();

    std::string loadError;
    bool ready = false;
    running_ = true;
    thread_ = std::thread(&Engine::run, this, source, chunkName, &loadError, &ready);
    {
        std::unique_lock<std::mutex> lock(qmu_);
        qcv_.wait(lock, [&] { return ready; });
    }
    if (!loadError.empty()) {
        thread_.join();
        running_ = false;
        error = loadError;
        return false;
    }
    post({"PROFILE_ACTIVATED", 0, ""});
    return true;
}

void Engine::stop() {
    if (!thread_.joinable()) return;
    abort_ = true;  // breaks out of the handler that is running right now
    post({"", 0, "", true});
    thread_.join();
    running_ = false;
}

void Engine::post(Event e) {
    {
        std::lock_guard<std::mutex> lock(qmu_);
        queue_.push_back(std::move(e));
    }
    qcv_.notify_all();
}

// ------------------------------------------------------------------ physical input

void Engine::onPhysicalButton(Button b, bool pressed) {
    const int i = static_cast<int>(b);
    {
        std::lock_guard<std::mutex> lock(smu_);
        auto& echoes = echo_[i];
        const auto now = Clock::now();
        while (!echoes.empty() && echoes.front().second < now) echoes.pop_front();
        if (!echoes.empty() && echoes.front().first == pressed) {
            echoes.pop_front();  // our own injected click coming back from the device
            return;
        }
        physical_[i] = pressed;
    }
    if (!running_ || (b == Button::Left && !primaryEvents_)) return;
    post({pressed ? "MOUSE_BUTTON_PRESSED" : "MOUSE_BUTTON_RELEASED", kEventButton[i], "mouse"});
}

void Engine::onExtraButton(int number, bool pressed) {
    if (number <= kButtonCount || number > kMaxButton) return;
    {
        std::lock_guard<std::mutex> lock(smu_);
        if (extra_[number] == pressed) return;  // ignore key auto-repeat
        extra_[number] = pressed;
    }
    if (running_) post({pressed ? "MOUSE_BUTTON_PRESSED" : "MOUSE_BUTTON_RELEASED", number, "mouse"});
}

void Engine::onGKey(int number, bool pressed) {
    if (running_) post({pressed ? "G_PRESSED" : "G_RELEASED", number, "kb"});
}

// ------------------------------------------------------------------ worker thread

void Engine::run(std::string source, std::string chunkName, std::string* loadError, bool* ready) {
    lua_State* L = luaL_newstate();
    *static_cast<Engine**>(lua_getextraspace(L)) = this;

    auto signalReady = [&](const std::string& err) {
        std::lock_guard<std::mutex> lock(qmu_);
        *loadError = err;
        *ready = true;
        qcv_.notify_all();
    };

    // base, table, string, math, utf8, coroutine, os (trimmed by the prelude)
    const luaL_Reg libs[] = {{LUA_GNAME, luaopen_base},       {LUA_TABLIBNAME, luaopen_table},
                             {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math},
                             {LUA_UTF8LIBNAME, luaopen_utf8},  {LUA_COLIBNAME, luaopen_coroutine},
                             {LUA_OSLIBNAME, luaopen_os}};
    for (const auto& lib : libs) {
        luaL_requiref(L, lib.name, lib.func, 1);
        lua_pop(L, 1);
    }

    const luaL_Reg api[] = {
        {"log", l_log},           {"clear_log", l_clearLog}, {"sleep", l_sleep},
        {"running_time", l_runningTime}, {"key", l_key},   {"modifier", l_modifier},
        {"lock", l_lock},         {"mbutton", l_mbutton},    {"mpressed", l_mpressed},
        {"move_rel", l_moveRel},  {"wheel", l_wheel},        {"move_to", l_moveTo},
        {"mouse_pos", l_mousePos}, {"primary", l_primary},   {nullptr, nullptr}};

    std::string err;
    if (luaL_loadstring(L, kPrelude) != LUA_OK) {
        err = lua_tostring(L, -1);
    } else {
        luaL_newlib(L, api);
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) err = lua_tostring(L, -1);
    }
    // abort long loops even if they never call Sleep()
    lua_sethook(L, &Engine::hook, LUA_MASKCOUNT, 10000);
    if (err.empty()) {
        if (luaL_loadbufferx(L, source.data(), source.size(), ("=" + chunkName).c_str(), "t") != LUA_OK ||
            lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* m = lua_tostring(L, -1);
            err = m ? m : "unknown error";
        } else if (lua_getglobal(L, "OnEvent") != LUA_TFUNCTION) {
            err = "The script does not define OnEvent(event, arg, family).";
        }
        lua_settop(L, 0);
    }
    if (!err.empty()) {
        lua_close(L);
        signalReady(err);
        return;
    }
    signalReady("");
    log_("Script loaded: " + chunkName + "\n");

    this->L_ = L;
    for (;;) {
        Event e;
        {
            std::unique_lock<std::mutex> lock(qmu_);
            qcv_.wait(lock, [&] { return !queue_.empty(); });
            e = std::move(queue_.front());
            queue_.pop_front();
        }
        if (e.stop) {
            // like G Hub: give the script a short moment to clean up
            abort_ = false;
            deadline_ = Clock::now() + std::chrono::seconds(1);
            hasDeadline_ = true;
            callOnEvent({"PROFILE_DEACTIVATED", 0, ""});
            break;
        }
        if (!abort_) callOnEvent(e);
    }
    this->L_ = nullptr;
    releaseAll();
    lua_close(L);
    running_ = false;
    log_("Script stopped.\n");
}

void Engine::callOnEvent(const Event& e) {
    lua_State* L = L_;
    lua_getglobal(L, "OnEvent");
    lua_pushstring(L, e.name.c_str());
    lua_pushinteger(L, e.arg);
    lua_pushstring(L, e.family.c_str());
    if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
        const char* m = lua_tostring(L, -1);
        std::string msg = m ? m : "unknown error";
        if (msg.find(kStopMarker) == std::string::npos)
            log_("Script error in OnEvent(" + e.name + ", " + std::to_string(e.arg) + "): " + msg + "\n");
        lua_pop(L, 1);
    }
}

bool Engine::shouldAbort() { return abort_ || (hasDeadline_ && Clock::now() > deadline_); }

void Engine::sleepMs(double ms) {
    auto end = Clock::now() + std::chrono::microseconds(static_cast<long long>(ms * 1000));
    if (hasDeadline_) end = std::min(end, deadline_);
    std::unique_lock<std::mutex> lock(qmu_);
    // woken early by stop(), which posts to the queue and sets abort_
    qcv_.wait_until(lock, end, [&] { return abort_.load(); });
}

void Engine::setJitter(int x, int y, unsigned seed) {
    jitterX_ = std::max(0, x);
    jitterY_ = std::max(0, y);
    rng_.seed(seed);
}

int Engine::pickJitter(int range) {
    return range ? std::uniform_int_distribution<int>(-range, range)(rng_) : 0;
}

// Each move picks a fresh offset and corrects for the previous one, so the
// error stays within the range instead of adding up over a long stroke.
void Engine::moveWithJitter(int dx, int dy) {
    if ((jitterX_ || jitterY_) && (dx || dy)) {
        const int nx = pickJitter(jitterX_), ny = pickJitter(jitterY_);
        dx += nx - offX_;
        dy += ny - offY_;
        offX_ = nx;
        offY_ = ny;
    }
    out_.move(dx, dy);
}

Backend* Engine::keyOutput() { return out_.supportsKeyboard() ? &out_ : keyFallback_; }

void Engine::releaseAll() {
    for (int i = 0; i < kButtonCount; i++) {
        bool held;
        {
            std::lock_guard<std::mutex> lock(smu_);
            held = synthetic_[i];
            synthetic_[i] = false;
        }
        if (held) out_.button(static_cast<Button>(i), false);
    }
    if (Backend* k = keyOutput())
        for (const KeyInfo* key : keysDown_) k->key(*key, false);
    keysDown_.clear();
}

// ------------------------------------------------------------------ Lua API

Engine* Engine::self(lua_State* L) { return *static_cast<Engine**>(lua_getextraspace(L)); }

void Engine::hook(lua_State* L, lua_Debug*) {
    if (self(L)->shouldAbort()) luaL_error(L, kStopMarker);
}

int Engine::l_log(lua_State* L) {
    size_t len = 0;
    const char* s = luaL_tolstring(L, 1, &len);
    self(L)->log_(std::string(s, len));
    return 0;
}

int Engine::l_clearLog(lua_State* L) {
    if (self(L)->onClearLog) self(L)->onClearLog();
    return 0;
}

int Engine::l_sleep(lua_State* L) {
    Engine* e = self(L);
    const double ms = luaL_optnumber(L, 1, 0);
    if (e->shouldAbort()) return luaL_error(L, kStopMarker);
    if (ms > 0) e->sleepMs(ms);
    if (e->shouldAbort()) return luaL_error(L, kStopMarker);
    return 0;
}

int Engine::l_runningTime(lua_State* L) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - self(L)->start_).count();
    lua_pushinteger(L, static_cast<lua_Integer>(ms));
    return 1;
}

int Engine::l_key(lua_State* L) {
    Engine* e = self(L);
    const bool down = lua_toboolean(L, 1);
    const int n = lua_gettop(L);
    for (int i = 2; i <= n; i++) {
        const KeyInfo* k = nullptr;
        std::string what;
        if (lua_type(L, i) == LUA_TNUMBER) {
            const int sc = static_cast<int>(lua_tonumber(L, i));
            k = findKeyByScancode(sc);
            char buf[32];
            std::snprintf(buf, sizeof(buf), "scancode 0x%X", sc);
            what = buf;
        } else {
            what = luaL_checkstring(L, i);
            k = findKeyByName(what);
            what = "key name \"" + what + "\"";
        }
        if (!k) {
            e->log_("Unknown " + what + "\n");
            continue;
        }
        Backend* outp = e->keyOutput();
        if (!outp) {
            if (!e->warnedKeyboard_) e->log_("This device cannot send keys; keyboard commands are ignored.\n");
            e->warnedKeyboard_ = true;
            continue;
        }
        outp->key(*k, down);
        if (down) e->keysDown_.insert(k);
        else e->keysDown_.erase(k);
    }
    return 0;
}

int Engine::l_modifier(lua_State* L) {
    Engine* e = self(L);
    const std::string name = lower(luaL_checkstring(L, 1));
    bool on;
    if (name == "shift" || name == "ctrl" || name == "alt")
        on = e->input_.modifierPressed("l" + name) || e->input_.modifierPressed("r" + name);
    else
        on = e->input_.modifierPressed(name);
    lua_pushboolean(L, on);
    return 1;
}

int Engine::l_lock(lua_State* L) {
    lua_pushboolean(L, self(L)->input_.lockOn(lower(luaL_checkstring(L, 1))));
    return 1;
}

int Engine::l_mbutton(lua_State* L) {
    Engine* e = self(L);
    Button b;
    if (!outputButton(toInt(L, 1), b)) return 0;
    const bool down = lua_toboolean(L, 2);
    {
        std::lock_guard<std::mutex> lock(e->smu_);
        e->echo_[static_cast<int>(b)].push_back({down, Clock::now() + kEchoWindow});
        e->synthetic_[static_cast<int>(b)] = down;
    }
    e->out_.button(b, down);
    return 0;
}

int Engine::l_mpressed(lua_State* L) {
    Engine* e = self(L);
    Button b;
    bool pressed = false;
    const int n = toInt(L, 1);
    std::lock_guard<std::mutex> lock(e->smu_);
    if (outputButton(n, b))
        pressed = e->physical_[static_cast<int>(b)] || e->synthetic_[static_cast<int>(b)];
    else if (n > kButtonCount && n <= kMaxButton)
        pressed = e->extra_[n];
    lua_pushboolean(L, pressed);
    return 1;
}

int Engine::l_moveRel(lua_State* L) {
    self(L)->moveWithJitter(toInt(L, 1), toInt(L, 2));
    return 0;
}

int Engine::l_wheel(lua_State* L) {
    self(L)->out_.wheel(toInt(L, 1));
    return 0;
}

int Engine::l_moveTo(lua_State* L) {
    // Hardware mice only move relatively, so steer towards the target.
    Engine* e = self(L);
    int left, top, w, h;
    e->input_.screenRect(lua_toboolean(L, 3), left, top, w, h);
    // with randomizing on, aim for a spot near the target; later relative moves wobble around it
    e->offX_ = e->pickJitter(e->jitterX_);
    e->offY_ = e->pickJitter(e->jitterY_);
    const int tx = left + static_cast<int>(std::lround(luaL_checknumber(L, 1) * (w - 1) / 65535.0)) + e->offX_;
    const int ty = top + static_cast<int>(std::lround(luaL_checknumber(L, 2) * (h - 1) / 65535.0)) + e->offY_;
    for (int i = 0; i < 3; i++) {
        int cx, cy;
        e->input_.cursorPos(cx, cy);
        if (cx == tx && cy == ty) break;
        e->out_.move(tx - cx, ty - cy);
        e->sleepMs(10);
    }
    return 0;
}

int Engine::l_mousePos(lua_State* L) {
    Engine* e = self(L);
    int left, top, w, h, x, y;
    e->input_.screenRect(false, left, top, w, h);
    e->input_.cursorPos(x, y);
    auto norm = [](int v, int size) {
        long n = std::lround(v * 65535.0 / std::max(size - 1, 1));
        return static_cast<lua_Integer>(std::clamp(n, 0L, 65535L));
    };
    lua_pushinteger(L, norm(x - left, w));
    lua_pushinteger(L, norm(y - top, h));
    return 2;
}

int Engine::l_primary(lua_State* L) {
    self(L)->primaryEvents_ = lua_toboolean(L, 1) != 0;
    return 0;
}
