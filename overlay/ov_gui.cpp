#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <thread>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdint>
#include <cmath>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include "ov_data.h"
#include "ov_native.h"

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

static HMODULE                  g_self = nullptr;
static std::atomic<bool>        g_unhook{ false };
static std::atomic<bool>        g_menu_open{ true };

static uintptr_t                g_present_slot = 0;
static PresentFn                g_present_orig = nullptr;

static ID3D11Device*            g_dev = nullptr;
static ID3D11DeviceContext*     g_ctx = nullptr;
static IDXGISwapChain*          g_sc = nullptr;
static ID3D11RenderTargetView*  g_rtv = nullptr;
static int                      g_rtv_w = 0, g_rtv_h = 0;
static int                      g_desc_check = 0;
static HWND                     g_hwnd = nullptr;
static bool                     g_gui_ready = false;
static bool                     g_gui_failed = false;
static float                    g_sw = 1920.0f, g_sh = 1080.0f;

static ov::Data                 g_data;
static bool                     g_data_ready = false;
static std::mutex               g_data_mtx;

static std::atomic<bool>        g_esp_on{ true };
static std::atomic<bool>        g_esp_name{ true };
static std::atomic<bool>        g_esp_dist{ true };
static std::atomic<bool>        g_god{ false };
static std::atomic<bool>        g_kill_req{ false };
static int                      g_kill_count = 0;
static float                    g_esp_h = 1.7f;
static float                    g_max_dist = 300.0f;
static float                    g_esp_col[4] = { 1.0f, 0.40f, 0.40f, 1.0f };
static int                      g_drawn = 0;

static ovn::State               g_nv;
static std::mutex               g_nv_mtx;
static std::atomic<bool>        g_god_state{ false };

static std::vector<std::string> g_logs;
static std::mutex               g_log_mtx;
static void logf(const char* fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt);
    _vsnprintf(buf, sizeof buf - 1, fmt, ap);
    va_end(ap);
    buf[sizeof buf - 1] = 0;
    {
        std::lock_guard<std::mutex> lk(g_log_mtx);
        if (g_logs.size() > 400) g_logs.erase(g_logs.begin(), g_logs.begin() + 150);
        g_logs.push_back(buf);
    }
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
    FILE* f = fopen("D:\\My Projects\\Honkai Impact\\overlay\\bh3_overlay.log", "a");
    if (f) { fprintf(f, "%s\n", buf); fclose(f); }
}

static const int kMaxHooks = 4;
struct HookRec { HWND h; WNDPROC orig; };
static HookRec g_hooks[kMaxHooks] = {};
static int     g_hook_n = 0;

static WNDPROC orig_of(HWND h) {
    for (int i = 0; i < g_hook_n; ++i) if (g_hooks[i].h == h) return g_hooks[i].orig;
    return nullptr;
}

static LRESULT CALLBACK hk_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(h, msg, wp, lp))
        return 1;
    WNDPROC o = orig_of(h);
    return o ? CallWindowProcW(o, h, msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

static void hook_one(HWND h) {
    if (!h || !IsWindow(h)) return;
    for (int i = 0; i < g_hook_n; ++i) {
        if (g_hooks[i].h != h) continue;
        if (GetWindowLongPtrW(h, GWLP_WNDPROC) == (LONG_PTR)hk_wndproc) return;
        g_hooks[i].orig = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)hk_wndproc);
        logf("[+] WndProc 被改回去了，已重新挂钩 hwnd=%p", (void*)h);
        return;
    }
    if (g_hook_n >= kMaxHooks) return;
    WNDPROC o = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)hk_wndproc);
    if (!o) { logf("[-] SetWindowLongPtr 失败 hwnd=%p", (void*)h); return; }
    g_hooks[g_hook_n].h = h;
    g_hooks[g_hook_n].orig = o;
    ++g_hook_n;
    logf("[+] WndProc 已挂钩 hwnd=%p", (void*)h);
}

static void install_wndproc() {
    if (!g_hwnd) return;
    hook_one(g_hwnd);
    HWND root = GetAncestor(g_hwnd, GA_ROOT);
    if (root && root != g_hwnd) hook_one(root);
}

static void tick_render_thread() {
    static uint64_t last_nv = 0, last_god = 0, last_log = 0;
    if (!g_data_ready) return;
    uint64_t now = GetTickCount64();
    if (now - last_nv >= 200) {
        last_nv = now;
        ovn::State s;
        ovn::refresh(g_data, s);
        {
            std::lock_guard<std::mutex> lk(g_nv_mtx);
            g_nv = s;
        }
        if (now - last_log >= 3000) {
            last_log = now;
            logf("[*] 怪 %zu 角色 %zu 相机 %s  avatar=%p monster=%p",
                 s.monsters.size(), s.avatars.size(), s.cam_ok ? "ok" : "无",
                 (void*)s.avatar_mgr, (void*)s.monster_mgr);
        }
    }
    if (now - last_god >= 60) {
        last_god = now;
        bool want = g_god.load();
        if (want != g_god_state.load()) {
            bool ok = ovn::god_mode(g_data, want);
            if (ok) g_god_state = want;
            logf("[*] 无敌 %s (%s)", want ? "开" : "关", ok ? "ok" : "patch 失败");
        }
    }
    if (g_kill_req.exchange(false)) {
        ovn::State s;
        {
            std::lock_guard<std::mutex> lk(g_nv_mtx);
            s = g_nv;
        }
        g_kill_count = ovn::kill_all(g_data, s);
        logf("[*] 秒杀：对 %d 个怪物调 DieNow", g_kill_count);
    }
}

static bool ensure_rtv(IDXGISwapChain* sc) {
    if (--g_desc_check > 0 && g_rtv) return true;
    g_desc_check = 60;

    DXGI_SWAP_CHAIN_DESC d{};
    if (FAILED(sc->GetDesc(&d))) return g_rtv != nullptr;
    int bw = (int)d.BufferDesc.Width, bh = (int)d.BufferDesc.Height;
    if (!bw || !bh) {
        RECT r{};
        if (g_hwnd && GetClientRect(g_hwnd, &r)) { bw = r.right - r.left; bh = r.bottom - r.top; }
        if (!bw || !bh) return g_rtv != nullptr;
    }
    g_sw = (float)bw; g_sh = (float)bh;
    if (g_rtv && g_rtv_w == bw && g_rtv_h == bh) return true;

    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
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
    if (FAILED(hr)) { logf("[-] CreateRenderTargetView 失败 %#x", (unsigned)hr); return false; }
    g_rtv_w = bw; g_rtv_h = bh;
    return true;
}

static bool init_gui(IDXGISwapChain* sc) {
    if (g_gui_ready || g_gui_failed) return g_gui_ready;
    g_sc = sc;
    DXGI_SWAP_CHAIN_DESC d{};
    if (SUCCEEDED(sc->GetDesc(&d))) g_hwnd = d.OutputWindow;
    if (!g_hwnd) g_hwnd = GetForegroundWindow();
    if (FAILED(sc->GetDevice(kIID_ID3D11Device, (void**)&g_dev)) || !g_dev) {
        logf("[-] GetDevice 失败"); g_gui_failed = true; return false;
    }
    g_dev->GetImmediateContext(&g_ctx);
    if (!g_ctx) { logf("[-] GetImmediateContext 失败"); g_gui_failed = true; return false; }
    g_sw = (float)d.BufferDesc.Width; g_sh = (float)d.BufferDesc.Height;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.MouseDrawCursor = true;

    const char* fonts[] = {
        "C:\\Windows\\Fonts\\msyh.ttc", "C:\\Windows\\Fonts\\msyh.ttf",
        "C:\\Windows\\Fonts\\msyhbd.ttc", "C:\\Windows\\Fonts\\simhei.ttf",
        "C:\\Windows\\Fonts\\simsun.ttc", "C:\\Windows\\Fonts\\deng.ttf"
    };
    bool font_ok = false;
    for (const char* fp : fonts) {
        if (GetFileAttributesA(fp) == INVALID_FILE_ATTRIBUTES) continue;
        ImFontConfig cfg;
        cfg.OversampleH = 2; cfg.OversampleV = 1; cfg.PixelSnapH = true;
        if (io.Fonts->AddFontFromFileTTF(fp, 17.0f, &cfg, io.Fonts->GetGlyphRangesChineseSimplifiedCommon())) {
            font_ok = true;
            logf("[+] 字体: %s", fp);
            break;
        }
    }
    if (!font_ok) { io.Fonts->AddFontDefault(); logf("[!] 没找到中文字体"); }

    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 6.0f;
    ImGui::GetStyle().FrameRounding = 4.0f;

    if (!ImGui_ImplWin32_Init(g_hwnd)) { logf("[-] ImGui_ImplWin32_Init 失败"); g_gui_failed = true; return false; }
    install_wndproc();
    if (!ImGui_ImplDX11_Init(g_dev, g_ctx)) { logf("[-] ImGui_ImplDX11_Init 失败"); g_gui_failed = true; return false; }
    g_gui_ready = true;
    logf("[+] ImGui 就绪 %dx%d", (int)g_sw, (int)g_sh);
    return true;
}

static void draw_esp() {
    if (!g_esp_on.load()) return;
    ovn::State s;
    {
        std::lock_guard<std::mutex> lk(g_nv_mtx);
        s = g_nv;
    }
    if (!s.cam_ok) return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImU32 col = ImGui::ColorConvertFloat4ToU32(
        ImVec4(g_esp_col[0], g_esp_col[1], g_esp_col[2], g_esp_col[3]));
    const ImU32 shadow = IM_COL32(0, 0, 0, 170);
    const bool show_name = g_esp_name.load();
    const bool show_dist = g_esp_dist.load();
    const float maxd = g_max_dist;

    int drawn = 0;
    for (const auto& e : s.monsters) {
        float sx = 0, sy = 0, hx = 0, hy = 0;
        if (!ovn::w2s(s.vp, e.x, e.y, e.z, s.w, s.h, sx, sy)) continue;
        if (!ovn::w2s(s.vp, e.x, e.y + g_esp_h, e.z, s.w, s.h, hx, hy)) continue;
        float bh = sy - hy;
        if (bh < 4.0f || bh > s.h) continue;
        float bw = bh * 0.45f;
        ImVec2 a(sx - bw * 0.5f, hy), b(sx + bw * 0.5f, sy);
        if (b.x < -8 || a.x > s.w + 8 || b.y < -8 || a.y > s.h + 8) continue;

        float dist = 0;
        if (!s.avatars.empty()) {
            float dx = e.x - s.avatars[0].x, dy = e.y - s.avatars[0].y, dz = e.z - s.avatars[0].z;
            dist = sqrtf(dx * dx + dy * dy + dz * dz);
        }
        if (show_dist && dist > maxd) continue;

        dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1), shadow, 0, 0, 3.0f);
        dl->AddRect(a, b, col, 0, 0, 1.5f);
        if (show_name) {
            char line[160];
            if (show_dist) snprintf(line, sizeof line, "%s  %.0fm", e.name.c_str(), dist);
            else           snprintf(line, sizeof line, "%s", e.name.c_str());
            dl->AddText(ImVec2(a.x, a.y - 16), col, line);
        }
        if (++drawn >= 400) break;
    }
    g_drawn = drawn;
}

static double g_fps = 0;
static uint64_t g_frames = 0;

static void ui_func() {
    ovn::State st;
    {
        std::lock_guard<std::mutex> lk(g_nv_mtx);
        st = g_nv;
    }

    ImGui::TextColored(st.cam_ok ? ImVec4(0.5f, 1, 0.5f, 1) : ImVec4(1, 0.7f, 0.3f, 1),
                       "%s", st.msg);
    ImGui::SameLine();
    ImGui::TextDisabled("| %.0f FPS", g_fps);
    ImGui::Separator();

    bool esp = g_esp_on.load();
    if (ImGui::Checkbox("ESP 绘制", &esp)) g_esp_on = esp;
    ImGui::SameLine();
    bool nm = g_esp_name.load();
    if (ImGui::Checkbox("显示名称", &nm)) g_esp_name = nm;
    ImGui::SameLine();
    bool ds = g_esp_dist.load();
    if (ImGui::Checkbox("显示距离", &ds)) g_esp_dist = ds;

    ImGui::ColorEdit4("方框颜色", g_esp_col, ImGuiColorEditFlags_NoInputs);
    ImGui::SetNextItemWidth(200);
    ImGui::SliderFloat("人物高度(m)", &g_esp_h, 1.0f, 2.4f, "%.2f");
    ImGui::SetNextItemWidth(200);
    ImGui::SliderFloat("最远距离(m)", &g_max_dist, 20.0f, 2000.0f, "%.0f");
    ImGui::TextDisabled("怪物 %zu，角色 %zu，实际绘制 %d", st.monsters.size(), st.avatars.size(), g_drawn);

    ImGui::Separator();

    bool god = g_god.load();
    if (ImGui::Checkbox("无敌（hook BaseMonoAvatar.BeHit）", &god)) {
        g_god = god;
        logf("[*] 无敌 %s", god ? "开" : "关");
    }
    ImGui::SameLine();
    ImGui::TextColored(g_god_state.load() ? ImVec4(0.5f, 1, 0.5f, 1) : ImVec4(1, 0.7f, 0.3f, 1),
                       "%s", g_god_state.load() ? "已生效" : "未生效");

    if (ImGui::Button("秒杀全部怪物", ImVec2(150, 0))) {
        g_kill_req = true;
        logf("[*] 请求秒杀");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("上次处理 %d 个", g_kill_count);

    ImGui::Separator();

    if (ImGui::CollapsingHeader("运行状态")) {
        ImGui::Text("UserAssembly %p", (void*)g_data.ua);
        ImGui::Text("AvatarManager  %p", (void*)st.avatar_mgr);
        ImGui::Text("MonsterManager %p", (void*)st.monster_mgr);
        ImGui::Text("Camera         %p", (void*)st.camera);
        ImGui::Text("类总数 %d", g_data.count);
    }
    ImGui::TextDisabled("热键：INSERT 菜单   F3 ESP   F5 秒杀   F6 无敌");
}

static void ui_logs() {
    if (ImGui::Button("清空")) { std::lock_guard<std::mutex> lk(g_log_mtx); g_logs.clear(); }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu 条", g_logs.size());
    ImGui::Separator();
    ImGui::BeginChild("log", ImVec2(0, 0), true);
    {
        std::lock_guard<std::mutex> lk(g_log_mtx);
        for (auto& s : g_logs) ImGui::TextUnformatted(s.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

static void draw_ui() {
    if (!g_menu_open.load()) return;
    static bool win_open = true;
    win_open = true;
    ImGui::SetNextWindowSize(ImVec2(520, 470), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(70, 70), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("崩坏3 · BH3##main", &win_open, ImGuiWindowFlags_None)) {
        if (ImGui::BeginTabBar("tabs")) {
            if (ImGui::BeginTabItem("功能区")) { ui_func(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("日志"))   { ui_logs(); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    if (!win_open) g_menu_open = false;
}

static HRESULT STDMETHODCALLTYPE hk_present(IDXGISwapChain* sc, UINT sync, UINT flags) {
    static std::atomic<bool> init_started{ false };
    bool expect = false;
    if (init_started.compare_exchange_strong(expect, true)) {
        init_gui(sc);
    }
    if (g_gui_ready && !g_gui_failed) {
        static bool pk[6] = { false };
        const int keys[6] = { VK_INSERT, VK_F3, VK_F5, VK_F6, VK_F7, VK_F8 };
        for (int i = 0; i < 6; ++i) {
            bool down = (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
            bool pressed = down && !pk[i];
            pk[i] = down;
            if (!pressed) continue;
            if (keys[i] == VK_INSERT) {
                g_menu_open = !g_menu_open;
            } else if (keys[i] == VK_F3) {
                g_esp_on = !g_esp_on.load();
                logf("[*] ESP %s", g_esp_on.load() ? "开" : "关");
            } else if (keys[i] == VK_F5) {
                g_kill_req = true;
            } else if (keys[i] == VK_F6) {
                bool v = !g_god.load();
                g_god = v;
                logf("[*] 无敌 %s", v ? "开" : "关");
            }
        }

        install_wndproc();
        tick_render_thread();

        if (ensure_rtv(sc)) {
            ImGuiIO& io = ImGui::GetIO();
            if (!g_menu_open.load()) { io.WantCaptureMouse = false; io.WantCaptureKeyboard = false; }
            io.MouseDrawCursor = g_menu_open.load();

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            static uint64_t t0 = 0; uint64_t t1 = GetTickCount64();
            ++g_frames;
            if (!t0) t0 = t1;
            if (t1 - t0 >= 500) {
                g_fps = (double)(g_frames * 1000) / (double)(t1 - t0);
                g_frames = 0; t0 = t1;
            }

            draw_esp();
            draw_ui();

            ImGui::Render();

            ID3D11RenderTargetView* old_rtv = nullptr;
            ID3D11DepthStencilView* old_dsv = nullptr;
            g_ctx->OMGetRenderTargets(1, &old_rtv, &old_dsv);
            UINT nvp = 1; D3D11_VIEWPORT old_vp{};
            g_ctx->RSGetViewports(&nvp, &old_vp);

            g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

            g_ctx->OMSetRenderTargets(1, &old_rtv, old_dsv);
            g_ctx->RSSetViewports(1, &old_vp);
            if (old_rtv) old_rtv->Release();
            if (old_dsv) old_dsv->Release();
        }
    }
    return g_present_orig(sc, sync, flags);
}

static bool hook_present() {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = 2;
    sd.BufferDesc.Height = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = GetForegroundWindow();
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    typedef HRESULT(WINAPI * Fn)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
                                 const D3D_FEATURE_LEVEL*, UINT, UINT,
                                 const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**,
                                 ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
    HMODULE d3d11 = LoadLibraryA("d3d11.dll");
    if (!d3d11) { logf("[-] 载入 d3d11.dll 失败"); return false; }
    Fn create = (Fn)GetProcAddress(d3d11, "D3D11CreateDeviceAndSwapChain");
    if (!create) { logf("[-] 找不到 D3D11CreateDeviceAndSwapChain"); return false; }

    IDXGISwapChain* sc = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL fl{};
    HRESULT hr = create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                        D3D11_SDK_VERSION, &sd, &sc, &dev, &fl, &ctx);
    if (FAILED(hr) || !sc) {
        logf("[-] 临时设备创建失败 %#x", (unsigned)hr);
        if (sc) sc->Release();
        if (dev) dev->Release();
        if (ctx) ctx->Release();
        return false;
    }
    void** vt = *(void***)sc;
    g_present_slot = (uintptr_t)vt[8];
    g_present_orig = (PresentFn)vt[8];
    DWORD old = 0;
    if (!VirtualProtect(&vt[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
        logf("[-] VirtualProtect 失败");
        sc->Release(); dev->Release(); ctx->Release();
        return false;
    }
    vt[8] = (void*)&hk_present;
    VirtualProtect(&vt[8], sizeof(void*), old, &old);
    logf("[+] Present 已挂钩 vtable[8]=%p orig=%p", (void*)g_present_slot, (void*)g_present_orig);
    sc->Release(); dev->Release(); ctx->Release();
    return true;
}

static void boot() {
    Sleep(1000);
    if (!hook_present()) return;

    for (int i = 0; i < 1200; ++i) {
        if (!GetModuleHandleA("UserAssembly.dll")) { Sleep(500); continue; }
        if (g_data.ready_ok()) break;
        {
            std::lock_guard<std::mutex> lk(g_data_mtx);
            if (g_data.init()) {
                g_data_ready = true;
                logf("[+] SDK 就绪: %d 个类 UA=%p String=%p",
                     g_data.count, (void*)g_data.ua, (void*)g_data.str_klass);
                break;
            }
        }
        Sleep(500);
    }
    if (!g_data_ready) logf("[-] SDK 初始化超时: %s", g_data.last_error);
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
