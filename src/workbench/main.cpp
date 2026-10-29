#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <wrl/client.h>
#include "ui.h"
#include "images.h"
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <cstdio>
#include <filesystem>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
using Microsoft::WRL::ComPtr;

namespace {
HWND g_hwnd;
ComPtr<ID3D11Device> g_device;
ComPtr<ID3D11DeviceContext> g_context;
ComPtr<IDXGISwapChain> g_swap;
ComPtr<ID3D11RenderTargetView> g_target;
UINT g_resize_w, g_resize_h;

void CreateTarget() {
    ComPtr<ID3D11Texture2D> back;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    g_device->CreateRenderTargetView(back.Get(), nullptr, &g_target);
}

void SavePlacement() {
    WINDOWPLACEMENT wp{sizeof(wp)};
    if (!GetWindowPlacement(g_hwnd, &wp)) return;
    const RECT& r = wp.rcNormalPosition;
    char b[96];
    std::snprintf(b, sizeof(b), "%ld %ld %ld %ld %d", r.left, r.top, r.right, r.bottom, wp.showCmd == SW_SHOWMAXIMIZED ? 1 : 0);
    wb::SetSetting("window", b);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 1;
    switch (msg) {
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) {
                g_resize_w = LOWORD(lp);
                g_resize_h = HIWORD(lp);
            }
            return 0;
        case WM_GETMINMAXINFO:
            reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = {900, 600};
            return 0;
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wp);
            std::vector<std::filesystem::path> files;
            for (UINT i = 0, n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0); i < n; i++) {
                std::wstring name(DragQueryFileW(drop, i, nullptr, 0), L'\0');
                DragQueryFileW(drop, i, name.data(), UINT(name.size() + 1));
                files.emplace_back(name);
            }
            DragFinish(drop);
            wb::Drop(std::move(files));
            return 0;
        }
        case WM_CLOSE:
            if (!wb::CanClose()) return 0;
            if (wb::Automate().page < 0) SavePlacement();
            DestroyWindow(hwnd);
            return 0;
        case WM_SYSCOMMAND:
            if ((wp & 0xfff0) == SC_KEYMENU) return 0;  // Alt does not open a window menu
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// the F6 menu's greens, deepened for a full window
void Style(float scale) {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    auto c = [](float r, float g, float b, float a = 1) { return ImVec4(r, g, b, a); };
    ImVec4* k = s.Colors;
    k[ImGuiCol_WindowBg] = c(.012f, .035f, .012f);
    k[ImGuiCol_ChildBg] = c(.02f, .055f, .02f);
    k[ImGuiCol_PopupBg] = c(.03f, .075f, .03f, .98f);
    k[ImGuiCol_Text] = c(.88f, .93f, .85f);
    k[ImGuiCol_TextDisabled] = c(.52f, .60f, .50f);
    k[ImGuiCol_Border] = c(.10f, .26f, .07f);
    k[ImGuiCol_Separator] = c(.10f, .30f, .06f);
    k[ImGuiCol_FrameBg] = c(.07f, .13f, .07f);
    k[ImGuiCol_FrameBgHovered] = c(.11f, .20f, .10f);
    k[ImGuiCol_FrameBgActive] = c(.14f, .26f, .12f);
    k[ImGuiCol_Button] = c(.08f, .24f, .04f);
    k[ImGuiCol_ButtonHovered] = c(.13f, .40f, .05f);
    k[ImGuiCol_ButtonActive] = c(.17f, .52f, .06f);
    k[ImGuiCol_Header] = c(.09f, .27f, .05f);
    k[ImGuiCol_HeaderHovered] = c(.12f, .36f, .06f);
    k[ImGuiCol_HeaderActive] = c(.16f, .48f, .07f);
    k[ImGuiCol_Tab] = c(.05f, .15f, .03f);
    k[ImGuiCol_TabSelected] = c(.10f, .32f, .05f);
    k[ImGuiCol_TabHovered] = c(.14f, .44f, .06f);
    k[ImGuiCol_TableHeaderBg] = c(.05f, .14f, .04f);
    k[ImGuiCol_TableRowBgAlt] = c(1, 1, 1, .025f);
    k[ImGuiCol_PlotHistogram] = c(.46f, .74f, .30f);
    k[ImGuiCol_CheckMark] = c(.60f, .86f, .42f);
    k[ImGuiCol_SliderGrab] = c(.40f, .62f, .28f);
    k[ImGuiCol_SliderGrabActive] = c(.55f, .82f, .38f);
    k[ImGuiCol_ScrollbarBg] = c(0, 0, 0, .15f);
    k[ImGuiCol_ScrollbarGrab] = c(.14f, .26f, .11f);
    k[ImGuiCol_ScrollbarGrabHovered] = c(.20f, .36f, .15f);
    k[ImGuiCol_TitleBgActive] = c(.10f, .53f, 0);
    k[ImGuiCol_ModalWindowDimBg] = c(0, 0, 0, .6f);
    k[ImGuiCol_NavHighlight] = c(.60f, .86f, .42f);
    s.WindowRounding = 6;
    s.ChildRounding = 6;
    s.FrameRounding = 4;
    s.PopupRounding = 6;
    s.GrabRounding = 4;
    s.TabRounding = 4;
    s.ScrollbarRounding = 6;
    s.FramePadding = ImVec2(8, 5);
    s.ItemSpacing = ImVec2(8, 6);
    s.WindowPadding = ImVec2(12, 10);
    s.ScrollbarSize = 13;
    s.ScaleAllSizes(scale);
}

// --capture: the frame as a PNG, for checking layouts without a person at the window
bool Capture(const std::filesystem::path& out) {
    ComPtr<ID3D11Texture2D> back, staging;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    D3D11_TEXTURE2D_DESC desc;
    back->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(g_device->CreateTexture2D(&desc, nullptr, &staging))) return false;
    g_context->CopyResource(staging.Get(), back.Get());
    D3D11_MAPPED_SUBRESOURCE map;
    if (FAILED(g_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return false;
    std::vector<uint8_t> px(size_t(desc.Width) * desc.Height * 4);
    for (UINT y = 0; y < desc.Height; y++) {
        const uint8_t* row = static_cast<const uint8_t*>(map.pData) + size_t(y) * map.RowPitch;
        for (UINT x = 0; x < desc.Width; x++) {
            uint8_t* o = &px[(size_t(y) * desc.Width + x) * 4];
            o[0] = row[x * 4 + 2];
            o[1] = row[x * 4 + 1];
            o[2] = row[x * 4];
            o[3] = 255;
        }
    }
    g_context->Unmap(staging.Get(), 0);
    return wb::WritePng(out, px.data(), desc.Width, desc.Height);
}

// Segoe UI with the Segoe MDL2 icons merged in
ImFont* LoadFont(const std::filesystem::path& fonts, const char* face, float size) {
    std::error_code ec;
    const auto path = fonts / face;
    if (!std::filesystem::exists(path, ec)) return nullptr;
    ImGuiIO& io = ImGui::GetIO();
    ImFont* font = io.Fonts->AddFontFromFileTTF(path.string().c_str(), size);
    for (const char* icons : {"segmdl2.ttf", "SegoeIcons.ttf"}) {
        if (!std::filesystem::exists(fonts / icons, ec)) continue;
        static const ImWchar ranges[] = {0xE700, 0xF8FF, 0};
        ImFontConfig cfg;
        cfg.MergeMode = true;
        cfg.GlyphOffset = ImVec2(0, size * .18f);
        cfg.GlyphMinAdvanceX = size * 1.05f;
        io.Fonts->AddFontFromFileTTF((fonts / icons).string().c_str(), size * .9f, &cfg, ranges);
        break;
    }
    return font;
}
}  // namespace

namespace wb {

// released after the frame that may still draw it
std::vector<ID3D11ShaderResourceView*> g_trash;
bool g_closed = false;  // after the last frame: release at once (globals go in any order)
Picture::~Picture() {
    if (!id) return;
    auto* view = reinterpret_cast<ID3D11ShaderResourceView*>(id);
    if (g_closed) view->Release();
    else g_trash.push_back(view);
}

ID3D11Device* Device() { return g_device.Get(); }
ID3D11DeviceContext* Context() { return g_context.Get(); }

std::shared_ptr<Picture> Upload(const uint8_t* bgra, uint32_t w, uint32_t h) {
    auto p = std::make_shared<Picture>();
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{bgra, w * 4, 0};
    ComPtr<ID3D11Texture2D> texture;
    ID3D11ShaderResourceView* view = nullptr;
    if (w && h && w <= 16384 && h <= 16384 && SUCCEEDED(g_device->CreateTexture2D(&desc, &data, &texture)) &&
        SUCCEEDED(g_device->CreateShaderResourceView(texture.Get(), nullptr, &view))) {
        p->w = w;
        p->h = h;
        p->id = reinterpret_cast<ImTextureID>(view);
    }
    return p;
}

void* MainWindow() { return g_hwnd; }

}  // namespace wb

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // --capture <png> [--page n] [--mode n] [--match] [--select n] [--size WxH]
    std::filesystem::path capture;
    int capture_w = 0, capture_h = 0;
    {
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; i < argc; i++) {
            const std::wstring a = argv[i];
            const bool more = i + 1 < argc;
            if (a == L"--capture" && more) capture = argv[++i];
            else if (a == L"--page" && more) wb::Automate().page = _wtoi(argv[++i]);
            else if (a == L"--mode" && more) wb::Automate().packs_mode = _wtoi(argv[++i]);
            else if (a == L"--select" && more) wb::Automate().select = _wtoi(argv[++i]);
            else if (a == L"--match") wb::Automate().match = true;
            else if (a == L"--size" && more) swscanf_s(argv[++i], L"%dx%d", &capture_w, &capture_h);
        }
        LocalFree(argv);
        if (!capture.empty() && wb::Automate().page < 0) wb::Automate().page = 0;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"TenchuRFDSwitchblade";
    RegisterClassExW(&wc);
    const float scale = GetDpiForSystem() / 96.f;
    g_hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName, L"RFD Switchblade", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             int(1480 * scale), int(900 * scale), nullptr, nullptr, instance, nullptr);
    int maximized = 0;
    RECT r{};
    if (sscanf_s(wb::Setting("window").c_str(), "%ld %ld %ld %ld %d", &r.left, &r.top, &r.right, &r.bottom, &maximized) == 5 &&
        r.right - r.left >= 600 && r.bottom - r.top >= 400 && MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) {
        WINDOWPLACEMENT wp{sizeof(wp)};
        wp.rcNormalPosition = r;
        wp.showCmd = SW_HIDE;
        SetWindowPlacement(g_hwnd, &wp);
    }

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = g_hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &g_swap,
                                               &g_device, nullptr, &g_context);
    if (FAILED(hr))
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &g_swap, &g_device,
                                           nullptr, &g_context);
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"Direct3D 11 is not available.", L"RFD Switchblade", MB_ICONERROR);
        return 1;
    }
    CreateTarget();
    if (capture_w > 0 && capture_h > 0) {
        RECT want{0, 0, capture_w, capture_h};
        AdjustWindowRectEx(&want, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_ACCEPTFILES);
        SetWindowPos(g_hwnd, nullptr, 0, 0, want.right - want.left, want.bottom - want.top, SWP_NOMOVE | SWP_NOZORDER);
        maximized = 0;
    }
    ShowWindow(g_hwnd, maximized ? SW_SHOWMAXIMIZED : SW_SHOWDEFAULT);
    UpdateWindow(g_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    wchar_t windows[MAX_PATH];
    GetWindowsDirectoryW(windows, MAX_PATH);
    const std::filesystem::path fonts = std::filesystem::path(windows) / "Fonts";
    ImFont* body = LoadFont(fonts, "segoeui.ttf", 17 * scale);
    ImFont* heading = LoadFont(fonts, "seguisb.ttf", 23 * scale);
    if (!body) io.FontGlobalScale = scale;
    Style(scale);
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device.Get(), g_context.Get());
    wb::Start(heading ? heading : body);

    int frames = 0, settled = 0;
    for (bool running = true; running;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;
        if (IsIconic(g_hwnd)) {
            Sleep(30);
            continue;
        }
        if (g_resize_w && g_resize_h) {
            g_target.Reset();
            g_swap->ResizeBuffers(0, g_resize_w, g_resize_h, DXGI_FORMAT_UNKNOWN, 0);
            g_resize_w = g_resize_h = 0;
            CreateTarget();
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        wb::Frame();
        ImGui::Render();
        const float clear[4] = {.012f, .035f, .012f, 1};
        g_context->OMSetRenderTargets(1, g_target.GetAddressOf(), nullptr);
        g_context->ClearRenderTargetView(g_target.Get(), clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        if (!capture.empty()) {
            frames++;
            settled = wb::Settled() ? settled + 1 : 0;
            if ((frames > 30 && settled > 40) || frames > 60 * 180) {
                Capture(capture);
                running = false;
            }
        }
        g_swap->Present(1, 0);
        for (auto* view : wb::g_trash) view->Release();
        wb::g_trash.clear();
    }
    wb::Stop();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    for (auto* view : wb::g_trash) view->Release();
    wb::g_trash.clear();
    wb::g_closed = true;
    return 0;
}
