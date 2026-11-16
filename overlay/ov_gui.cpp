#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include "ov_data.h"
#include "ov_native.h"
#include "ov_act.h"
#include "ov_lua.h"
#include "ov_mcp.h"

#include "ui/App.h"
#include "ui/State.h"
#include "ui/UiCore.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

extern "C" HRESULT WINAPI D3DCompile(
    LPCVOID pSrcData, SIZE_T SrcDataSize, LPCSTR pSourceName,
    const D3D_SHADER_MACRO* pDefines, ID3DInclude* pInclude,
    LPCSTR pEntrypoint, LPCSTR pTarget, UINT Flags1, UINT Flags2,
    ID3DBlob** ppCode, ID3DBlob** ppErrorMsgs) {
    typedef HRESULT(WINAPI * Fn)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*,
                                 ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT,
                                 ID3DBlob**, ID3DBlob**);
    static Fn fn = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        const char* names[] = { "d3dcompiler_47.dll", "d3dcompiler_46.dll", "d3dcompiler_43.dll" };
        for (const char* n : names) {
            HMODULE m = LoadLibraryA(n);
            if (!m) continue;
            fn = (Fn)GetProcAddress(m, "D3DCompile");
            if (fn) break;
        }
    }
    if (!fn) {
        if (ppErrorMsgs) *ppErrorMsgs = nullptr;
        return E_FAIL;
    }
    return fn(pSrcData, SrcDataSize, pSourceName, pDefines, pInclude,
              pEntrypoint, pTarget, Flags1, Flags2, ppCode, ppErrorMsgs);
}

static const GUID kIID_ID3D11Device = {
    0xdb6f6ddb, 0xac77, 0x4e88, { 0x82, 0x53, 0x81, 0x9d, 0xf9, 0xbb, 0xf1, 0x40 }
};
static const GUID kIID_ID3D11Texture2D = {
    0x6f15aaf2, 0xd208, 0x4e89, { 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c }
};

typedef HRESULT(STDMETHODCALLTYPE* PresentFn)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* ResizeFn)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

static HMODULE                  g_self = nullptr;
static std::atomic<bool>        g_unhook{ false };

static PresentFn                g_present_orig = nullptr;
static ResizeFn                 g_resize_orig = nullptr;
static std::recursive_mutex      g_render_mutex;
static std::atomic<bool>         g_ui_visible{ false };

static ID3D11Device*            g_dev = nullptr;
static ID3D11DeviceContext*     g_ctx = nullptr;
static IDXGISwapChain*          g_sc = nullptr;
static ID3D11RenderTargetView*  g_rtv = nullptr;
static int                      g_rtv_w = 0, g_rtv_h = 0;
static HWND                     g_hwnd = nullptr;
static bool                     g_gui_ready = false;
static bool                     g_win32_ready = false;
static bool                     g_dx11_ready = false;
static float                    g_sw = 1920.0f, g_sh = 1080.0f;

static ov::Data                 g_data;
static std::atomic<bool>        g_data_ready{ false };

// Only the window thread touches g_data and g_game_nv. Present owns g_nv/UI.
static ovn::State               g_nv;
static ovn::State               g_game_nv;
static ova::ActState            g_game_act;
static ova::ActState            g_act;
static ova::ActCtx              g_act_ctx;
static int                      g_act_kill_marks = 0;
static ovlua::Session           g_lua;
static std::atomic<bool>        g_god_state{ false };
struct GameExchange {
    ui::Settings cfg;
    ui::Requests req;
    ui::Status status;
    ovn::State snapshot;
    ova::ActState act;
    bool snapshot_ready = false;
};
static GameExchange             g_game_exchange;
static std::mutex               g_game_mutex;
static std::atomic<bool>        g_sample_pending{ false };
static UINT                     g_sample_msg = 0;
static HHOOK                    g_sample_hook = nullptr;
static std::atomic<DWORD>        g_game_thread_id{ 0 };
static std::atomic<HWND>         g_sample_hwnd{ nullptr };
static std::mutex               g_sampling_mutex;
static void tick_game_thread();
static LRESULT CALLBACK game_message_hook(int code, WPARAM wp, LPARAM lp);

struct InputMessage { HWND hwnd; UINT msg; WPARAM wp; LPARAM lp; };
static std::deque<InputMessage>  g_input_queue;
static std::mutex               g_input_mutex;
static std::mutex               g_hooks_mutex;

static const int kMaxHooks = 4;
struct HookRec { HWND h; WNDPROC orig; };
static HookRec g_hooks[kMaxHooks] = {};
static int     g_hook_n = 0;

static WNDPROC orig_of(HWND h) {
    std::lock_guard<std::mutex> lock(g_hooks_mutex);
    for (int i = 0; i < g_hook_n; ++i) if (g_hooks[i].h == h) return g_hooks[i].orig;
    return nullptr;
}

static bool is_mouse_msg(UINT m) {
    switch (m) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
    case WM_MOUSEWHEEL:  case WM_MOUSEHWHEEL:
    case WM_SETCURSOR:
        return true;
    default:
        return false;
    }
}

static bool is_key_msg(UINT m) {
    switch (m) {
    case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
    case WM_CHAR:    case WM_SYSCHAR: case WM_UNICHAR: case WM_DEADCHAR:
        return true;
    default:
        return false;
    }
}

static LRESULT CALLBACK hk_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    const bool vis = g_ui_visible.load();
    if (vis && (is_mouse_msg(msg) || is_key_msg(msg) ||
                msg == WM_SETFOCUS || msg == WM_KILLFOCUS)) {
        // ImGui belongs to Present. Its input queue must not be modified
        // concurrently by the game's window thread.
        std::lock_guard<std::mutex> lock(g_input_mutex);
        if (g_input_queue.size() < 1024) g_input_queue.push_back({h, msg, wp, lp});
    }

    if (vis && (is_mouse_msg(msg) || is_key_msg(msg)))
        return 0;

    WNDPROC o = orig_of(h);
    if (msg == WM_NCDESTROY) {
        std::lock_guard<std::mutex> lock(g_hooks_mutex);
        for (int i = 0; i < g_hook_n; ++i) {
            if (g_hooks[i].h == h) {
                g_hooks[i] = g_hooks[--g_hook_n];
                break;
            }
        }
    }
    return o ? CallWindowProcW(o, h, msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

static void hook_one(HWND h) {
    if (!h || !IsWindow(h)) return;
    std::lock_guard<std::mutex> lock(g_hooks_mutex);
    for (int i = 0; i < g_hook_n; ++i) {
        if (g_hooks[i].h != h) continue;
        // Another overlay may have chained us. Re-hooking it would form a cycle.
        if (GetWindowLongPtrW(h, GWLP_WNDPROC) == (LONG_PTR)g_hooks[i].orig)
            SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)hk_wndproc);
        return;
    }
    if (g_hook_n >= kMaxHooks) return;
    WNDPROC o = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)hk_wndproc);
    if (!o) return;
    g_hooks[g_hook_n].h = h;
    g_hooks[g_hook_n].orig = o;
    ++g_hook_n;
}

static void install_wndproc() {
    if (!g_hwnd) return;
    hook_one(g_hwnd);
}

static void tick_game_thread() {
    if (GetCurrentThreadId() != g_game_thread_id || g_unhook) return;
    std::unique_lock<std::mutex> sampling_lock(g_sampling_mutex, std::try_to_lock);
    if (!sampling_lock.owns_lock()) return;
    // MCP 的函数调用只在游戏主线程执行，避免跨线程调 IL2CPP。
    ovmcp::OnGameThreadTick();
    static uint64_t last_init = 0;
    ui::Settings cfg;
    ui::Requests rq;
    ui::Status st;
    {
        std::lock_guard<std::mutex> lock(g_game_mutex);
        cfg = g_game_exchange.cfg;
        rq = g_game_exchange.req;
        g_game_exchange.req = ui::Requests();
        st = g_game_exchange.status;
    }
    const uint64_t now = GetTickCount64();
    if (rq.rescan) {
        g_data_ready = false;
        g_data = ov::Data();
        g_game_nv = ovn::State();
        last_init = 0;
    }
    g_data.clear_seg_cache();
    if (!g_data_ready && (!last_init || now - last_init >= 500)) {
        last_init = now;
        g_data_ready = g_data.init();
    }
    if (g_data_ready) {
        ovn::refresh(g_data, g_game_nv);
        if (cfg.god != g_god_state.load()) {
            if (ovn::god_mode(g_data, cfg.god)) g_god_state = cfg.god;
        }
        if (cfg.inf_sp != ovn::sp_applied()) ovn::infinite_sp(g_data, cfg.inf_sp);
        if (cfg.no_cd != ovn::cd_applied()) ovn::no_skill_cd(g_data, cfg.no_cd);
        if (cfg.crit != ovn::crit_applied()) ovn::hundred_crit(g_data, cfg.crit);
        if (cfg.dmg_on || cfg.god || cfg.ohk || ovn::dmg_applied())
            ovn::damage_hook(g_data, cfg.dmg_on, cfg.dmg_mult, cfg.god, cfg.ohk);
        if (cfg.atk_on) ovn::attack_speed(g_data, g_game_nv, true, cfg.atk_mult);
        else if (ovn::atk_speed_applied()) ovn::attack_speed(g_data, g_game_nv, false, 1.0f);
        if (rq.kill_all && g_game_nv.cam_ok && !g_game_nv.avatars.empty())
            st.kill_count += (int)ovn::kill_all(g_data, g_game_nv);
    } else {
        g_game_nv = ovn::State();
        lstrcpynA(g_game_nv.msg, g_data.last_error, (int)sizeof g_game_nv.msg);
    }

    // 活动模块独立驱动：活动场景没有主战场 manager，不能挂在 g_data_ready 上
    if (g_data.rt.ready()) {
        if (rq.act_scan_again) {
            g_game_act = ova::ActState();
            g_act_ctx.reset_scan();
        } else if (rq.act_refresh && g_game_act.ready) {
            g_game_act.t = 0;  // 强制下一次 refresh 立即重读实体
        }
        if (cfg.act_on || cfg.act_god || cfg.act_kill || g_game_act.ready) {
            ova::refresh(g_data, g_game_act, g_act_ctx, 2);
            if (g_game_act.ready) {
                if (cfg.act_kill || rq.act_kill_all)
                    g_act_kill_marks += ova::kill_all(g_data, g_game_act);
                if (cfg.act_god) ova::god_ally(g_data, g_game_act);
                ova::enforce_kills(g_data, g_game_act);
            }
        }
    } else {
        g_game_act = ova::ActState();
    }

    // Lua 控制台：扫描分帧进行，执行只在游戏主线程
    if (g_data.ready_ok())
    {
        if (rq.lua_scan) ovlua::start_scan(g_data, g_lua);
        ovlua::step_scan(g_data, g_lua);
        if (rq.lua_trust && cfg.lua_sel >= 0 && cfg.lua_sel < g_lua.n)
            g_lua.trusted[cfg.lua_sel] = true;
        if (rq.lua_exec)
        {
            std::string lua_out;
            const bool lua_ok = ovlua::exec(g_data, g_lua, cfg.lua_sel, rq.lua_code.c_str(), lua_out);
            st.lua_ok = lua_ok;
            snprintf(st.lua_out, sizeof st.lua_out, "%s", lua_out.c_str());
        }
    }
    st.lua_ready = g_lua.lua_klass && g_lua.mi && g_lua.n > 0;
    st.lua_scanning = g_lua.scanning;
    st.lua_state_n = g_lua.n;
    st.lua_trusted_n = 0;
    for (int i = 0; i < g_lua.n; ++i) if (g_lua.trusted[i]) ++st.lua_trusted_n;
    st.lua_state = (cfg.lua_sel >= 0 && cfg.lua_sel < g_lua.n) ? g_lua.states[cfg.lua_sel] : 0;
    snprintf(st.lua_msg, sizeof st.lua_msg, "%s", g_lua.msg);

    st.avatar_mgr = g_game_nv.avatar_mgr;
    st.monster_mgr = g_game_nv.monster_mgr;
    st.avatar_n = (int)g_game_nv.avatars.size();
    st.monster_n = (int)g_game_nv.monsters.size();
    st.gadget_n = (int)g_game_nv.gadgets.size();
    st.npc_n = (int)g_game_nv.npcs.size();
    st.cam_ok = g_game_nv.cam_ok;
    st.camera = g_game_nv.camera;
    st.cam_w = g_game_nv.w;
    st.cam_h = g_game_nv.h;
    snprintf(st.cam_msg, sizeof st.cam_msg, "%s", g_game_nv.msg);
    st.stamp = now;
    st.data_ready = g_data_ready;
    st.class_count = g_data.count;
    st.ua = g_data.ua;
    st.god_applied = g_god_state.load() && ovn::god_true_applied();
    st.inf_sp_applied = ovn::sp_applied();
    st.no_cd_applied = ovn::cd_applied();
    st.crit_applied = ovn::crit_applied();
    st.dmg_applied = ovn::dmg_applied();
    st.ohk_applied = ovn::ohk_applied();
    st.atk_speed_applied = ovn::atk_speed_applied();

    ovmcp::UpdateGameStatus(g_data.ua, g_data_ready,
                            (int)g_game_nv.avatars.size(),
                            (int)g_game_nv.monsters.size(),
                            g_game_nv.cam_ok, g_game_nv.msg);

    st.act_ready = g_game_act.ready;
    st.act_in = g_game_act.in_act;
    st.act_n = g_game_act.n;
    st.act_ally = g_game_act.ally_n;
    st.act_enemy = g_game_act.enemy_n;
    st.act_scan_ms = g_game_act.scan_ms;
    st.act_killed = g_act_kill_marks;
    snprintf(st.act_msg, sizeof st.act_msg, "%s", g_game_act.msg);
    for (int i = 0; i < 32; ++i) {
        ui::Status::ActUnit& u = st.act_u[i];
        u = ui::Status::ActUnit();
        if (i >= g_game_act.n) continue;
        const ova::Actor& a = g_game_act.list[i];
        u.camp = a.camp;
        u.raw_camp = a.raw_camp;
        u.hp = a.hp;
        u.max_hp = a.max_hp > 0 ? a.max_hp : a.raw_max;
        u.tag = a.camp == 2 ? a.monster_id : a.avatar_id;
        u.slot = i;
    }
    {
        std::lock_guard<std::mutex> lock(g_game_mutex);
        g_game_exchange.snapshot = g_game_nv;
        g_game_exchange.act = g_game_act;
        g_game_exchange.status = st;
        g_game_exchange.snapshot_ready = true;
    }
}

static LRESULT CALLBACK game_message_hook(int code, WPARAM wp, LPARAM lp) {
    if (code >= 0 && wp == PM_REMOVE && lp) {
        MSG* msg = (MSG*)lp;
        if (msg->message == g_sample_msg && msg->hwnd == g_sample_hwnd.load() &&
            g_sample_pending.exchange(false)) {
            static thread_local bool sampling = false;
            if (!sampling && !g_unhook) {
                sampling = true;
                try { tick_game_thread(); }
                catch (...) { g_data_ready = false; }
                sampling = false;
            }
            msg->message = WM_NULL;
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

static void tick_render_thread() {
    {
        std::lock_guard<std::mutex> lock(g_game_mutex);
        g_game_exchange.cfg = ui::Cfg();
        g_game_exchange.req.kill_all |= ui::Req().kill_all;
        g_game_exchange.req.rescan |= ui::Req().rescan;
        g_game_exchange.req.act_kill_all |= ui::Req().act_kill_all;
        g_game_exchange.req.act_scan_again |= ui::Req().act_scan_again;
        g_game_exchange.req.act_refresh |= ui::Req().act_refresh;
        g_game_exchange.req.lua_scan |= ui::Req().lua_scan;
        g_game_exchange.req.lua_trust |= ui::Req().lua_trust;
        if (ui::Req().lua_exec)
        {
            g_game_exchange.req.lua_exec = true;
            g_game_exchange.req.lua_code = ui::Req().lua_code;
        }
        ui::Req() = ui::Requests();
        if (g_game_exchange.snapshot_ready) {
            std::swap(g_nv, g_game_exchange.snapshot);
            std::swap(g_act, g_game_exchange.act);
            ui::Status& st = ui::St();
            const float fps = st.fps;
            const double frame_ms = st.frame_ms;
            const int esp_drawn = st.esp_drawn;
            st = g_game_exchange.status;
            st.fps = fps; st.frame_ms = frame_ms; st.esp_drawn = esp_drawn;
            g_game_exchange.snapshot_ready = false;
        }
    }
    if (g_sample_hook && !g_sample_pending.exchange(true)) {
        if (!PostMessageW(g_hwnd, g_sample_msg, 0, 0)) g_sample_pending = false;
    }
    if (g_nv.t && GetTickCount64() - g_nv.t > 500) {
        ovn::invalidate_camera(g_nv, "等待游戏线程采样");
        g_nv.avatars.clear();
        g_nv.monsters.clear();
    }
    if (g_act.t && GetTickCount64() - g_act.t > 800) {
        g_act = ova::ActState();
    }
}

static void release_rtv() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    g_rtv_w = g_rtv_h = 0;
}

static void restore_wndprocs() {
    std::lock_guard<std::mutex> lock(g_hooks_mutex);
    for (int i = 0; i < g_hook_n;) {
        const HookRec& hook = g_hooks[i];
        bool restored = !IsWindow(hook.h);
        if (IsWindow(hook.h) &&
            GetWindowLongPtrW(hook.h, GWLP_WNDPROC) == (LONG_PTR)hk_wndproc) {
            restored = SetWindowLongPtrW(hook.h, GWLP_WNDPROC, (LONG_PTR)hook.orig) != 0;
        }
        if (restored) g_hooks[i] = g_hooks[--g_hook_n];
        else ++i;
    }
    // Keep records if another overlay still chains our WndProc.
}

static void reset_gui() {
    g_ui_visible = false;
    g_gui_ready = false;
    g_sample_hwnd = nullptr;
    g_game_thread_id = 0;
    if (g_sample_hook) { UnhookWindowsHookEx(g_sample_hook); g_sample_hook = nullptr; }
    g_sample_pending = false;
    restore_wndprocs();
    if (g_dx11_ready) { ImGui_ImplDX11_Shutdown(); g_dx11_ready = false; }
    if (g_win32_ready) { ImGui_ImplWin32_Shutdown(); g_win32_ready = false; }
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
    release_rtv();
    if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
    g_sc = nullptr;
    g_hwnd = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_input_mutex);
        g_input_queue.clear();
    }
}

static bool ensure_rtv(IDXGISwapChain* sc) {
    if (g_rtv) return true;
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(sc->GetBuffer(0, kIID_ID3D11Texture2D, (void**)&bb)) || !bb) return false;
    D3D11_TEXTURE2D_DESC td{};
    bb->GetDesc(&td);
    D3D11_RENDER_TARGET_VIEW_DESC rd{};
    rd.Format = td.Format;
    rd.ViewDimension = (td.SampleDesc.Count > 1) ? D3D11_RTV_DIMENSION_TEXTURE2DMS
                                                 : D3D11_RTV_DIMENSION_TEXTURE2D;
    HRESULT hr = g_dev->CreateRenderTargetView(bb, &rd, &g_rtv);
    bb->Release();
    if (FAILED(hr)) return false;
    g_rtv_w = (int)td.Width; g_rtv_h = (int)td.Height;
    g_sw = (float)td.Width; g_sh = (float)td.Height;
    return true;
}

static bool init_gui(IDXGISwapChain* sc) {
    DXGI_SWAP_CHAIN_DESC d{};
    if (FAILED(sc->GetDesc(&d)) || !d.OutputWindow || !IsWindow(d.OutputWindow)) return false;
    DWORD pid = 0;
    const DWORD thread_id = GetWindowThreadProcessId(d.OutputWindow, &pid);
    if (!thread_id || pid != GetCurrentProcessId()) return false;
    g_sc = sc;
    g_hwnd = d.OutputWindow;
    if (FAILED(sc->GetDevice(kIID_ID3D11Device, (void**)&g_dev)) || !g_dev) {
        reset_gui(); return false;
    }
    g_dev->GetImmediateContext(&g_ctx);
    if (!g_ctx) { reset_gui(); return false; }
    g_sw = (float)d.BufferDesc.Width; g_sh = (float)d.BufferDesc.Height;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.MouseDrawCursor = true;

    {
        char err[256] = { 0 };
        ui::SetUiScale(ui::Cfg().ui_scale);
        ui::BuildFonts(io.Fonts, ui::UiScale(), err, (int)sizeof err);
    }

    ui::ApplyTheme(ui::UiScale());

    g_win32_ready = ImGui_ImplWin32_Init(g_hwnd);
    if (!g_win32_ready) { reset_gui(); return false; }
    g_dx11_ready = ImGui_ImplDX11_Init(g_dev, g_ctx);
    if (!g_dx11_ready) { reset_gui(); return false; }
    g_game_thread_id = thread_id;
    g_sample_hwnd = g_hwnd;
    g_sample_hook = SetWindowsHookExW(WH_GETMESSAGE, game_message_hook, g_self, thread_id);
    if (!g_sample_hook) { reset_gui(); return false; }
    install_wndproc();

    ui::AppInit();
    g_gui_ready = true;
    return true;
}

struct smooth_box {
    float x = 0, y = 0, w = 0, h = 0;
    bool  init = false;
    uint64_t seen = 0;
};

static std::unordered_map<uintptr_t, smooth_box> g_smooth;

static void smooth_prune(uint64_t now) {
    if (g_smooth.size() < 1024) return;
    for (auto it = g_smooth.begin(); it != g_smooth.end();) {
        if (now - it->second.seen > 2000) it = g_smooth.erase(it);
        else ++it;
    }
}

static void smooth_apply(uintptr_t key, float& x, float& y, float& w, float& h,
                         uint64_t now, float dt, bool on) {
    if (!on) return;
    smooth_box& b = g_smooth[key];
    b.seen = now;
    const float nx = x, ny = y, nw = w, nh = h;
    if (!b.init) {
        b.init = true; b.x = nx; b.y = ny; b.w = nw; b.h = nh;
        return;
    }
    const float jump = fabsf(nx - b.x) + fabsf(ny - b.y) + fabsf(nw - b.w) + fabsf(nh - b.h);
    if (jump > 90.0f) {
        b.x = nx; b.y = ny; b.w = nw; b.h = nh;
    } else {
        float k = dt * 28.0f;
        if (k > 0.92f) k = 0.92f;
        if (k < 0.20f) k = 0.20f;
        b.x += (nx - b.x) * k;
        b.y += (ny - b.y) * k;
        b.w += (nw - b.w) * k;
        b.h += (nh - b.h) * k;
    }
    x = b.x; y = b.y; w = b.w; h = b.h;
}

static int find_local_avatar(const ovn::State& s) {
    if (s.avatars.empty()) return -1;
    if (s.avatars.size() == 1) return 0;
    int best = -1;
    float bd = 1e18f;
    for (int i = 0; i < (int)s.avatars.size(); ++i) {
        const ovn::Ent& a = s.avatars[(size_t)i];
        const float dx = a.x - s.cam_pos[0];
        const float dy = a.y - s.cam_pos[1];
        const float dz = a.z - s.cam_pos[2];
        const float d = dx * dx + dy * dy + dz * dz;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static inline float dist_between(const float p[3], float x, float y, float z) {
    const float dx = x - p[0], dy = y - p[1], dz = z - p[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static void draw_box(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col) {
    dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1), IM_COL32(0, 0, 0, 170), 0, 0, 3.0f);
    dl->AddRect(a, b, col, 0, 0, 1.5f);
}

static bool project_ent(const ovn::State& s, float sw, float sh, float x, float y, float z, float h,
                        float& bx, float& by, float& bw, float& bh) {
    float sx = 0, sy = 0, hx = 0, hy = 0;
    if (!ovn::w2s(s.vp, x, y, z, s.w, s.h, sx, sy)) return false;
    if (!ovn::w2s(s.vp, x, y + h, z, s.w, s.h, hx, hy)) return false;
    if (!(sx == sx) || !(sy == sy) || !(hx == hx) || !(hy == hy)) return false;
    if (s.w > 1.0f && s.h > 1.0f && (s.w != sw || s.h != sh)) {
        const float kx = sw / s.w;
        const float ky = sh / s.h;
        sx *= kx; hx *= kx;
        sy *= ky; hy *= ky;
    }
    const float top = fminf(sy, hy);
    const float bot = fmaxf(sy, hy);
    const float vh = bot - top;
    if (vh < 3.0f) return false;
    if (vh > sh * 3.0f) return false;
    bx = sx;
    by = top;
    bh = vh;
    bw = vh * 0.45f;
    if (bw < 2.0f) bw = 2.0f;
    return true;
}

static void draw_esp(float dt) {
    ui::St().esp_drawn = 0;
    const ui::Settings& cfg = ui::Cfg();
    if (!cfg.esp_on) {
        ui::St().esp_drawn = 0;
        g_smooth.clear();
        return;
    }

    const ovn::State& s = g_nv;
    if (!s.cam_ok || !s.vp_valid) return;

    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float sw = (ds.x > 1.0f) ? ds.x : s.w;
    const float sh = (ds.y > 1.0f) ? ds.y : s.h;
    if (sw <= 1.0f || sh <= 1.0f) return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImU32 col = ImGui::ColorConvertFloat4ToU32(
        ImVec4(cfg.esp_col[0], cfg.esp_col[1], cfg.esp_col[2], cfg.esp_col[3]));
    const ImU32 col_team = IM_COL32(90, 220, 140, 235);
    const ImU32 col_gadget = ImGui::ColorConvertFloat4ToU32(
        ImVec4(cfg.gadget_col[0], cfg.gadget_col[1], cfg.gadget_col[2], cfg.gadget_col[3]));
    const ImU32 col_npc = ImGui::ColorConvertFloat4ToU32(
        ImVec4(cfg.npc_col[0], cfg.npc_col[1], cfg.npc_col[2], cfg.npc_col[3]));

    const bool show_box  = cfg.esp_box;
    const bool show_name = cfg.esp_name;
    const bool show_dist = cfg.esp_dist;
    const bool smooth    = cfg.esp_smooth;
    const float maxd   = cfg.max_dist;
    const float esp_h  = cfg.esp_h;
    const uint64_t now = GetTickCount64();

    ImFont* f = ui::EspFont(cfg.esp_font);
    if (!f) f = ui::Fonts().small ? ui::Fonts().small : ImGui::GetFont();
    const float fsz = f ? (cfg.esp_font > 1.0f ? cfg.esp_font : (f ? f->FontSize : 20.0f)) : 20.0f;

    const int li = find_local_avatar(s);
    const bool avatar_pos_ok = li >= 0 &&
        (fabsf(s.avatars[(size_t)li].x) + fabsf(s.avatars[(size_t)li].y) +
         fabsf(s.avatars[(size_t)li].z)) > 0.01f;
    const float* ref = s.player_ok ? s.player_pos :
        (avatar_pos_ok ? &s.avatars[(size_t)li].x : s.cam_pos);
    const bool ref_ok = (fabsf(ref[0]) + fabsf(ref[1]) + fabsf(ref[2])) > 0.01f;

    smooth_prune(now);

    int drawn = 0;
    char line[192];

    // 从屏幕顶部中心到目标框顶边中心的射线。
    float ray_w = cfg.ray_w;
    if (!(ray_w >= 0.5f)) ray_w = 0.5f;
    if (ray_w > 12.0f) ray_w = 12.0f;
    auto ray_to = [&](const ImVec2& a, const ImVec2& b) {
        if (!cfg.ray_on) return;
        dl->AddLine(ImVec2(sw * 0.5f, 0.0f),
                    ImVec2((a.x + b.x) * 0.5f, a.y),
                    IM_COL32(210, 220, 255, 70), ray_w);
    };

    for (const auto& e : s.monsters) {
        float bx = 0, by = 0, bw = 0, bh = 0;
        if (!project_ent(s, sw, sh, e.x, e.y, e.z, esp_h, bx, by, bw, bh)) continue;

        const float dist = dist_between(ref, e.x, e.y, e.z);
        if (show_dist && dist > maxd) continue;

        float x = bx, y = by, w = bw, h = bh;
        smooth_apply(e.p, x, y, w, h, now, dt, smooth);
        ImVec2 a(x - w * 0.5f, y), b(x + w * 0.5f, y + h);
        if (b.x < -32.0f || a.x > sw + 32.0f || b.y < -32.0f || a.y > sh + 32.0f) continue;

        ray_to(a, b);
        if (show_box) draw_box(dl, a, b, col);
        if (show_name) {
            const char* nm = e.label.empty() ? e.name.c_str() : e.label.c_str();
            if (show_dist) snprintf(line, sizeof line, "%s  %.0fm", nm, dist);
            else           snprintf(line, sizeof line, "%s", nm);
            const ImVec2 ts = f->CalcTextSizeA(fsz, FLT_MAX, 0.0f, line, nullptr);
            const float tx = (a.x + b.x) * 0.5f - ts.x * 0.5f;
            const float ty = a.y - ts.y - cfg.esp_off;
            // 先描边再填字，避免亮背景看不清
            const ImU32 sh = IM_COL32(0, 0, 0, 200);
            dl->AddText(f, fsz, ImVec2(tx - 1.0f, ty), sh, line);
            dl->AddText(f, fsz, ImVec2(tx + 1.0f, ty), sh, line);
            dl->AddText(f, fsz, ImVec2(tx, ty - 1.0f), sh, line);
            dl->AddText(f, fsz, ImVec2(tx, ty + 1.0f), sh, line);
            dl->AddText(f, fsz, ImVec2(tx, ty), col, line);
        }
        if (++drawn >= 400) break;
    }

    if (cfg.esp_team && s.avatars.size() > 1) {
        for (int i = 0; i < (int)s.avatars.size(); ++i) {
            if (i == li) continue;
            const ovn::Ent& e = s.avatars[(size_t)i];
            float bx = 0, by = 0, bw = 0, bh = 0;
            if (!project_ent(s, sw, sh, e.x, e.y, e.z, esp_h, bx, by, bw, bh)) continue;

            const float dist = dist_between(ref, e.x, e.y, e.z);
            if (show_dist && dist > maxd) continue;

            float x = bx, y = by, w = bw, h = bh;
            smooth_apply(e.p, x, y, w, h, now, dt, smooth);
            ImVec2 a(x - w * 0.5f, y), b(x + w * 0.5f, y + h);
            if (b.x < -32.0f || a.x > sw + 32.0f || b.y < -32.0f || a.y > sh + 32.0f) continue;

            ray_to(a, b);
            if (show_box) draw_box(dl, a, b, col_team);
            if (show_name) {
                if (show_dist) snprintf(line, sizeof line, "%s  %.0fm", e.name.c_str(), dist);
                else           snprintf(line, sizeof line, "%s", e.name.c_str());
                const ImVec2 ts = f->CalcTextSizeA(fsz, FLT_MAX, 0.0f, line, nullptr);
                const float tx = (a.x + b.x) * 0.5f - ts.x * 0.5f;
                const float ty = a.y - ts.y - cfg.esp_off;
                const ImU32 sh = IM_COL32(0, 0, 0, 200);
                dl->AddText(f, fsz, ImVec2(tx - 1.0f, ty), sh, line);
                dl->AddText(f, fsz, ImVec2(tx + 1.0f, ty), sh, line);
                dl->AddText(f, fsz, ImVec2(tx, ty - 1.0f), sh, line);
                dl->AddText(f, fsz, ImVec2(tx, ty + 1.0f), sh, line);
                dl->AddText(f, fsz, ImVec2(tx, ty), col_team, line);
            }
            if (++drawn >= 400) break;
        }
    }

    auto draw_pjms_list = [&](const std::vector<ovn::Ent>& list, ImU32 color, float height) {
        for (const auto& e : list) {
            float bx = 0, by = 0, bw = 0, bh = 0;
            if (!project_ent(s, sw, sh, e.x, e.y, e.z, height, bx, by, bw, bh)) continue;
            const float dist = dist_between(ref, e.x, e.y, e.z);
            if (ref_ok && dist > maxd) continue;
            float x = bx, y = by, w = bw, hh = bh;
            smooth_apply(e.p, x, y, w, hh, now, dt, smooth);
            ImVec2 a(x - w * 0.5f, y), b(x + w * 0.5f, y + hh);
            if (b.x < -32.0f || a.x > sw + 32.0f || b.y < -32.0f || a.y > sh + 32.0f) continue;

            ray_to(a, b);
            if (show_box) draw_box(dl, a, b, color);
            if (show_name) {
                const char* nm = e.label.empty() ? e.name.c_str() : e.label.c_str();
                if (show_dist) snprintf(line, sizeof line, "%s  %.0fm", nm, dist);
                else           snprintf(line, sizeof line, "%s", nm);
                const ImVec2 ts = f->CalcTextSizeA(fsz, FLT_MAX, 0.0f, line, nullptr);
                const float tx = (a.x + b.x) * 0.5f - ts.x * 0.5f;
                const float ty = a.y - ts.y - cfg.esp_off;
                const ImU32 sh = IM_COL32(0, 0, 0, 200);
                dl->AddText(f, fsz, ImVec2(tx - 1.0f, ty), sh, line);
                dl->AddText(f, fsz, ImVec2(tx + 1.0f, ty), sh, line);
                dl->AddText(f, fsz, ImVec2(tx, ty - 1.0f), sh, line);
                dl->AddText(f, fsz, ImVec2(tx, ty + 1.0f), sh, line);
                dl->AddText(f, fsz, ImVec2(tx, ty), color, line);
            }
            if (++drawn >= 400) break;
        }
    };
    if (cfg.gadget_on) draw_pjms_list(s.gadgets, col_gadget, 0.8f);
    if (cfg.gadget_on) draw_pjms_list(s.props, col_gadget, 0.8f);
    if (cfg.npc_on)    draw_pjms_list(s.npcs, col_npc, esp_h);

    ui::St().esp_drawn = drawn;
}

static void update_fps() {
    static uint64_t t0 = 0;
    static uint64_t frames = 0;
    ++frames;
    uint64_t t1 = GetTickCount64();
    if (!t0) t0 = t1;
    if (t1 - t0 >= 500) {
        ui::Status& st = ui::St();
        st.fps      = (float)((double)(frames * 1000) / (double)(t1 - t0));
        st.frame_ms = st.fps > 0.5f ? 1000.0 / (double)st.fps : 0.0;
        frames = 0; t0 = t1;
    }
}

static void render_overlay(IDXGISwapChain* sc) {
    std::lock_guard<std::recursive_mutex> render_lock(g_render_mutex);
    // Loading screens may replace the swap chain/device/output window.
    DXGI_SWAP_CHAIN_DESC desc{};
    const bool valid_desc = SUCCEEDED(sc->GetDesc(&desc)) && IsWindow(desc.OutputWindow);
    if (!valid_desc) return;
    if (g_gui_ready && (g_sc != sc || g_hwnd != desc.OutputWindow)) {
        // Ignore auxiliary windows while the selected game window is alive.
        if (g_hwnd != desc.OutputWindow && IsWindow(g_hwnd) &&
            (!IsWindowVisible(desc.OutputWindow) ||
             (IsWindowVisible(g_hwnd) &&
              GetForegroundWindow() != GetAncestor(desc.OutputWindow, GA_ROOT)))) return;
        reset_gui();
    }
    if (!g_gui_ready) init_gui(sc);
    if (g_gui_ready) {
        static bool pk[9] = { false };
        const int keys[9] = { VK_INSERT, VK_F3, VK_F5, VK_F6, VK_F7, VK_F8, VK_F9, VK_F10, VK_F11 };
        for (int i = 0; i < 9; ++i) {
            bool down = (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
            bool pressed = down && !pk[i];
            pk[i] = down;
            if (!pressed) continue;
            ui::Settings& cfg = ui::Cfg();
            if (keys[i] == VK_INSERT) {
                ui::AppSetVisible(!ui::AppVisible());
            } else if (keys[i] == VK_F3) {
                cfg.esp_on = !cfg.esp_on;
            } else if (keys[i] == VK_F5) {
                ui::Req().kill_all = true;
            } else if (keys[i] == VK_F6) {
                cfg.god = !cfg.god;
            } else if (keys[i] == VK_F7) {
                cfg.inf_sp = !cfg.inf_sp;
            } else if (keys[i] == VK_F8) {
                cfg.no_cd = !cfg.no_cd;
            } else if (keys[i] == VK_F9) {
                cfg.crit = !cfg.crit;
            } else if (keys[i] == VK_F10) {
                cfg.atk_on = !cfg.atk_on;
            } else if (keys[i] == VK_F11) {
                if (cfg.tp_on) ovn::teleport_req().store(true);
            }
        }

        tick_render_thread();
        g_ui_visible = ui::AppVisible();
        {
            std::deque<InputMessage> input;
            {
                std::lock_guard<std::mutex> lock(g_input_mutex);
                input.swap(g_input_queue);
            }
            for (const InputMessage& msg : input)
                if (msg.hwnd == g_hwnd)
                    ImGui_ImplWin32_WndProcHandler(msg.hwnd, msg.msg, msg.wp, msg.lp);
        }

        if (ensure_rtv(sc)) {
            ImGuiIO& io = ImGui::GetIO();
            const bool vis = ui::AppVisible();
            if (!vis) { io.WantCaptureMouse = false; io.WantCaptureKeyboard = false; }
            io.MouseDrawCursor = vis;

            static uint64_t last_tick = 0;
            uint64_t now = GetTickCount64();
            float dt = last_tick ? (float)(now - last_tick) / 1000.0f : (1.0f / 60.0f);
            last_tick = now;
            if (dt <= 0.0f) dt = 1.0f / 60.0f;

            // 界面缩放变更：字体图集只能在 NewFrame() 之前重建，这里取走
            // AppFrame() 登记的请求（带 0.16s 去抖，滑块拖动中不会反复重建）。
            float rebuild_scale = 1.0f;
            if (ui::TakeFontRebuild(&rebuild_scale)) {
                char err[256] = { 0 };
                ui::SetUiScale(rebuild_scale);
                ui::BuildFonts(io.Fonts, rebuild_scale, err, (int)sizeof err);
                ui::ApplyTheme(rebuild_scale);
                ImGui_ImplDX11_InvalidateDeviceObjects();
            }

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            update_fps();

            draw_esp(dt);
            if (vis) ui::AppFrame(dt);
            g_ui_visible = ui::AppVisible();

            ImGui::Render();

            ID3D11RenderTargetView* old_rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
            ID3D11DepthStencilView* old_dsv = nullptr;
            g_ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, old_rtvs, &old_dsv);
            g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

            g_ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, old_rtvs, old_dsv);
            for (auto* rtv : old_rtvs) if (rtv) rtv->Release();
            if (old_dsv) old_dsv->Release();
            // Do not retain any back-buffer reference between Presents. The
            // engine can resize/recreate buffers while changing scenes.
            release_rtv();
        }
    }
}

static HRESULT STDMETHODCALLTYPE hk_present(IDXGISwapChain* sc, UINT sync, UINT flags) {
    if (!g_unhook && !(flags & DXGI_PRESENT_TEST)) render_overlay(sc);
    const HRESULT hr = g_present_orig(sc, sync, flags);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        std::lock_guard<std::recursive_mutex> render_lock(g_render_mutex);
        if (g_sc == sc) reset_gui();
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE hk_resize(IDXGISwapChain* sc, UINT count, UINT w, UINT h,
                                           DXGI_FORMAT format, UINT flags) {
    std::lock_guard<std::recursive_mutex> render_lock(g_render_mutex);
    if (g_sc == sc) release_rtv();
    return g_resize_orig(sc, count, w, h, format, flags);
}

static bool hook_present() {
    // Never create the probe swap chain against a foreground/game window.
    HWND probe = CreateWindowExW(0, L"STATIC", L"bh3-overlay-probe", WS_POPUP,
                                 0, 0, 2, 2, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!probe) return false;
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = 2;
    sd.BufferDesc.Height = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = probe;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    typedef HRESULT(WINAPI * Fn)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
                                 const D3D_FEATURE_LEVEL*, UINT, UINT,
                                 const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**,
                                 ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
    HMODULE d3d11 = LoadLibraryA("d3d11.dll");
    if (!d3d11) { DestroyWindow(probe); return false; }
    Fn create = (Fn)GetProcAddress(d3d11, "D3D11CreateDeviceAndSwapChain");
    if (!create) { DestroyWindow(probe); return false; }

    IDXGISwapChain* sc = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL fl{};
    HRESULT hr = create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                        D3D11_SDK_VERSION, &sd, &sc, &dev, &fl, &ctx);
    if (FAILED(hr) || !sc) {
        if (sc) sc->Release();
        if (dev) dev->Release();
        if (ctx) ctx->Release();
        DestroyWindow(probe);
        return false;
    }
    void** vt = *(void***)sc;
    g_present_orig = (PresentFn)vt[8];
    g_resize_orig = (ResizeFn)vt[13];
    DWORD old = 0;
    const SIZE_T bytes = 6 * sizeof(void*);
    if (!VirtualProtect(&vt[8], bytes, PAGE_READWRITE, &old)) {
        sc->Release(); dev->Release(); ctx->Release();
        DestroyWindow(probe);
        return false;
    }
    InterlockedExchangePointer((PVOID volatile*)&vt[13], (PVOID)&hk_resize);
    InterlockedExchangePointer((PVOID volatile*)&vt[8], (PVOID)&hk_present);
    VirtualProtect(&vt[8], bytes, old, &old);
    sc->Release(); dev->Release(); ctx->Release();
    DestroyWindow(probe);
    return true;
}

static void boot() {
    // DXGI and thread hooks retain callbacks into this module for process life.
    // Prevent a caller's FreeLibrary from leaving dangling vtable/WndProc slots.
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_PIN,
                            (LPCWSTR)&boot, &pinned)) return;
    g_sample_msg = RegisterWindowMessageW(L"bh3-overlay-game-sample-v1");
    if (!g_sample_msg) return;
    bool hooked = false;
    for (int i = 0; i < 1200 && !g_unhook; ++i) {
        if (hook_present()) { hooked = true; break; }
        Sleep(500);
    }
    // 渲染 hook 就绪后再起 MCP，避免在加载器锁或初始化阶段开线程。
    if (hooked)
        ovmcp::Sync(ui::Cfg().mcp_on, (unsigned short)ui::Cfg().mcp_port);
}

static DWORD WINAPI boot_thread(LPVOID) { boot(); return 0; }

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
        HANDLE h = CreateThread(nullptr, 0, boot_thread, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    } else if (reason == DLL_PROCESS_DETACH) {
        g_unhook = true;
    }
    return TRUE;
}
