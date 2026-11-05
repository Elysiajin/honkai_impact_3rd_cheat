// Run the real overlay callbacks against a hidden, software-rendered DX11
// window in this process. No game process or injector is involved.
#include "../ov_gui.cpp"
#include <cstdlib>

static int checks = 0;
static void check(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main() {
    g_self = GetModuleHandleW(nullptr);
    g_sample_msg = RegisterWindowMessageW(L"bh3-overlay-lifecycle-test");
    HWND window = CreateWindowExW(0, L"STATIC", L"overlay-lifecycle-test", WS_POPUP,
                                  0, 0, 640, 480, nullptr, nullptr, g_self, nullptr);
    check(window != nullptr, "create hidden test window");
    const WNDPROC original = (WNDPROC)GetWindowLongPtrW(window, GWLP_WNDPROC);
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 1;
    desc.BufferDesc.Width = 640; desc.BufferDesc.Height = 480;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    IDXGISwapChain* chain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL level{};
    check(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
        0, nullptr, 0, D3D11_SDK_VERSION, &desc, &chain, &device, &level, &context)),
        "create software DX11 swap chain");
    void** table = *(void***)chain;
    g_present_orig = (PresentFn)table[8];
    g_resize_orig = (ResizeFn)table[13];
    check(init_gui(chain), "initialize real ImGui and game-thread message hook");
    check(g_sample_hook && g_gui_ready, "thread hook and UI ready");
    check(ensure_rtv(chain), "create back-buffer RTV");
    // Holding a back-buffer reference must reproduce DXGI's resize rejection.
    check(FAILED(g_resize_orig(chain, 1, 800, 600, DXGI_FORMAT_UNKNOWN, 0)),
          "retained RTV reproduces resize failure");
    check(SUCCEEDED(hk_resize(chain, 1, 800, 600, DXGI_FORMAT_UNKNOWN, 0)) && !g_rtv,
          "resize callback releases back buffer and succeeds");
    check(ensure_rtv(chain) && g_rtv_w == 800 && g_rtv_h == 600,
          "recreate RTV with actual resized buffer dimensions");
    release_rtv();
    for (int i = 0; i < 3; ++i) {
        render_overlay(chain);
        check(!g_rtv, "frame leaves no retained back-buffer reference");
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        check(!g_sample_pending && !g_data_ready, "sampling without game runtime is safe and retryable");
    }
    check(SUCCEEDED(hk_resize(chain, 1, 640, 480, DXGI_FORMAT_UNKNOWN, 0)),
          "resize after multiple frames succeeds");
    reset_gui();
    check(!g_gui_ready && !g_sample_hook && !g_ctx && !g_dev && !g_rtv &&
          !ImGui::GetCurrentContext(), "cleanup releases UI, thread hook, and device resources");
    check((WNDPROC)GetWindowLongPtrW(window, GWLP_WNDPROC) == original && !g_hook_n,
          "cleanup restores window procedure");
    check(init_gui(chain), "reinitialize after swap-chain lifecycle reset");
    render_overlay(chain);
    check(!g_rtv && ui::Fonts().body, "rebuilt font atlas renders after reinitialization");
    reset_gui();
    context->Release(); device->Release(); chain->Release();
    DestroyWindow(window);
    std::printf("PASS: %d real DX11 lifecycle checks\n", checks);
    return 0;
}
