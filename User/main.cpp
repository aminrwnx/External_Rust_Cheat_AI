/*
 * main.cpp - NullkD External ESP for Rust
 *
 * Creates a transparent fullscreen overlay with ImGui menu
 * and communicates with the kernel driver for game memory reads.
 */

#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <string>
#include <vector>

/* DirectX 11 */
#include <d3d11.h>

/* ImGui */
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_dx11.h"

/* DWM for transparent overlay */
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

/* Our headers */
#include "driver_comm.h"
#include "rust_sdk.h"

/* ── Globals ─────────────────────────────────────────────────────── */

static HWND                     g_hWnd              = nullptr;
static WNDCLASSEXW              g_wc                = {};
static int                      g_ScreenW           = 0;
static int                      g_ScreenH           = 0;

/* DirectX 11 */
static ID3D11Device*            g_pd3dDevice        = nullptr;
static ID3D11DeviceContext*     g_pd3dContext       = nullptr;
static IDXGISwapChain*          g_pSwapChain        = nullptr;
static ID3D11RenderTargetView*  g_pRenderTargetView = nullptr;

/* Driver & SDK */
static DriverComm               g_Driver;
static RustSDK*                 g_SDK               = nullptr;

/* Menu / ESP state */
static bool                     g_ShowMenu          = true;
static bool                     g_Running           = true;
static bool                     g_overlayHidden     = true;

/* ESP config */
static bool  g_espEnabled       = true;
static bool  g_espBoxes         = true;
static bool  g_espNames         = true;
static bool  g_espDistance       = true;
static bool  g_espSnaplines     = false;
static bool  g_espHealthBar     = false;
static bool  g_espVisCheck      = false;
static bool  g_espShowSleepers  = false;
static bool  g_espShowWounded   = true;
static float g_espMaxDist       = 1000.0f;

/* Cached player list — rebuilt every 500ms, drawn every frame */
static std::vector<PlayerData>  g_cachedPlayers;

/* Colors */
static ImU32 COL_ENEMY   = IM_COL32(255,  60,  60, 255);
static ImU32 COL_TEAM    = IM_COL32( 60, 255,  60, 255);
static ImU32 COL_SLEEPER = IM_COL32(160, 160, 160, 180);
static ImU32 COL_WOUNDED = IM_COL32(255, 180,   0, 255);
static ImU32 COL_SNAP    = IM_COL32(255, 255, 255,  80);
static ImU32 COL_WHITE   = IM_COL32(255, 255, 255, 255);

/* Cache */
static ViewMatrix               g_ViewMatrix        = {};
static uint64_t                 g_LocalTeam         = 0;
static Vec3                     g_LocalPos          = {};
static int                      g_PlayerCount       = 0;

/* ── Forward Declarations ────────────────────────────────────────── */

static bool CreateOverlayWindow();
static bool InitD3D11();
static void CreateRenderTarget();
static void CleanupRenderTarget();
static void CleanupD3D11();
static void RenderMenu();
static void RenderESP();

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

/* ── Window Procedure ────────────────────────────────────────────── */

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0,
                (UINT)LOWORD(lParam), (UINT)HIWORD(lParam),
                DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

/* ── Overlay Window ──────────────────────────────────────────────── */

static bool CreateOverlayWindow()
{
    g_ScreenW = GetSystemMetrics(SM_CXSCREEN);
    g_ScreenH = GetSystemMetrics(SM_CYSCREEN);

    g_wc.cbSize        = sizeof(WNDCLASSEXW);
    g_wc.style         = CS_HREDRAW | CS_VREDRAW;
    g_wc.lpfnWndProc   = WndProc;
    g_wc.hInstance      = GetModuleHandleW(nullptr);
    /* Generic Windows class name — blends in with system windows */
    g_wc.lpszClassName  = L"MSCTFIME UI";
    RegisterClassExW(&g_wc);

    g_hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED |
        WS_EX_TOOLWINDOW |   /* Hide from taskbar + alt-tab */
        WS_EX_NOACTIVATE,    /* Never steal focus from game */
        g_wc.lpszClassName,
        L"",                  /* Empty title */
        WS_POPUP,
        0, 0, g_ScreenW, g_ScreenH,
        nullptr, nullptr, g_wc.hInstance, nullptr
    );
    if (!g_hWnd) return false;

    SetLayeredWindowAttributes(g_hWnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(g_hWnd, &margins);

    /* Remove from DWM window band to reduce visibility */
    SetWindowDisplayAffinity(g_hWnd, WDA_EXCLUDEFROMCAPTURE);

    ShowWindow(g_hWnd, SW_SHOWNOACTIVATE);
    UpdateWindow(g_hWnd);
    return true;
}

/* ── DirectX 11 ──────────────────────────────────────────────────── */

static bool InitD3D11()
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Width                   = g_ScreenW;
    sd.BufferDesc.Height                  = g_ScreenH;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 0;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = g_hWnd;
    sd.SampleDesc.Count                   = 1;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;
    sd.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION,
        &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dContext
    );
    if (FAILED(hr)) return false;

    CreateRenderTarget();
    return true;
}

static void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
        pBackBuffer->Release();
    }
}

static void CleanupRenderTarget()
{
    if (g_pRenderTargetView) {
        g_pRenderTargetView->Release();
        g_pRenderTargetView = nullptr;
    }
}

static void CleanupD3D11()
{
    CleanupRenderTarget();
    if (g_pSwapChain)   { g_pSwapChain->Release();   g_pSwapChain   = nullptr; }
    if (g_pd3dContext)   { g_pd3dContext->Release();   g_pd3dContext  = nullptr; }
    if (g_pd3dDevice)    { g_pd3dDevice->Release();    g_pd3dDevice   = nullptr; }
}

/* ── Click-through toggle ────────────────────────────────────────── */

static void SetClickThrough(bool clickThrough)
{
    LONG_PTR exStyle = GetWindowLongPtrW(g_hWnd, GWL_EXSTYLE);
    if (clickThrough) {
        exStyle |= (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
    } else {
        exStyle &= ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
    }
    SetWindowLongPtrW(g_hWnd, GWL_EXSTYLE, exStyle);

    if (!clickThrough) {
        /* Bring overlay to front and give it focus so ImGui can receive input */
        SetForegroundWindow(g_hWnd);
        SetFocus(g_hWnd);
    }
}

/* ── ImGui Menu ──────────────────────────────────────────────────── */

static void RenderMenu()
{
    if (!g_ShowMenu) return;

    ImGui::SetNextWindowSize(ImVec2(420, 380), ImGuiCond_FirstUseEver);
    ImGui::Begin("NullkD - Rust ESP", &g_ShowMenu, ImGuiWindowFlags_NoCollapse);

    /* ── Status ─────────────────────────────────────────────── */

    if (g_Driver.IsConnected()) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "[+] Driver Connected");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "[-] Driver Disconnected");
        if (ImGui::Button("Reconnect Driver")) g_Driver.Init();
    }

    if (g_SDK && g_SDK->IsAttached()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f),
            " | Rust PID: %lu | Players: %d", g_SDK->GetPID(), g_PlayerCount);
    } else {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), " | Rust: Not Found");
        if (g_SDK && ImGui::Button("Attach to Rust")) {
            if (g_SDK->Attach())
                printf("[+] Attached to Rust!\n");
        }
    }

    ImGui::Separator();

    /* ── Tabs ───────────────────────────────────────────────── */

    if (ImGui::BeginTabBar("MainTabs")) {

        /* ── ESP Tab ────────────────────────────────────────── */
        if (ImGui::BeginTabItem("ESP")) {
            ImGui::Checkbox("Enable ESP", &g_espEnabled);
            ImGui::Separator();

            if (g_espEnabled) {
                ImGui::Checkbox("Boxes",      &g_espBoxes);
                ImGui::Checkbox("Names",      &g_espNames);
                ImGui::Checkbox("Distance",   &g_espDistance);
                ImGui::Checkbox("Snaplines",  &g_espSnaplines);
                ImGui::Checkbox("Health Bar", &g_espHealthBar);
                ImGui::Separator();
                ImGui::Checkbox("Visibility Check", &g_espVisCheck);
                ImGui::Checkbox("Show Sleepers",    &g_espShowSleepers);
                ImGui::Checkbox("Show Wounded",     &g_espShowWounded);
                ImGui::Separator();
                ImGui::SliderFloat("Max Distance", &g_espMaxDist, 50.f, 2500.f, "%.0fm");
            }

            ImGui::Text("Screen: %dx%d", g_ScreenW, g_ScreenH);
            ImGui::EndTabItem();
        }

        /* ── Aim Tab ────────────────────────────────────────── */
        if (ImGui::BeginTabItem("Aim")) {
            ImGui::Text("Aim features (future).");
            ImGui::EndTabItem();
        }

        /* ── Misc Tab ───────────────────────────────────────── */
        if (ImGui::BeginTabItem("Misc")) {
            if (ImGui::Checkbox("Hidden from Capture", &g_overlayHidden)) {
                SetWindowDisplayAffinity(g_hWnd,
                    g_overlayHidden ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("ON = invisible in screenshots/recordings\nOFF = visible in screenshots/recordings");
            ImGui::Separator();
            if (ImGui::Button("Exit (END)")) {
                g_Running = false;
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

/* ── ESP: Refresh cached player list (called every 500ms) ────────── */

static void RefreshPlayerCache()
{
    g_cachedPlayers.clear();
    if (!g_SDK || !g_SDK->IsAttached()) return;

    g_SDK->RefreshEntityList();
    int count = g_SDK->GetEntityCount();
    if (count <= 0) return;

    g_LocalPos = g_SDK->GetCameraPosition();

    for (int i = 0; i < count; i++) {
        uintptr_t entity = g_SDK->GetEntity(i);
        if (!entity) continue;
        if (!g_SDK->IsPlayer(entity)) continue;

        PlayerData player;
        if (!g_SDK->ReadPlayer(entity, player)) continue;

        /* Skip dead */
        if (player.lifestate != 0) continue;

        /* Compute distance */
        player.distance = (player.position - g_LocalPos).Length();

        g_cachedPlayers.push_back(player);
    }
}

/* ── ESP Rendering (draws from cache — zero driver reads) ────────── */

static void RenderESP()
{
    if (!g_espEnabled || !g_SDK || !g_SDK->IsAttached()) return;

    ImDrawList* draw = ImGui::GetForegroundDrawList();

    /* Read view matrix (1 driver read per frame — needed for camera movement) */
    if (!g_SDK->GetViewMatrix(g_ViewMatrix)) return;

    /* Refresh player cache every 500ms (all heavy driver reads happen here) */
    static DWORD lastRefresh = 0;
    DWORD now = GetTickCount();
    if (now - lastRefresh > 500 || lastRefresh == 0) {
        RefreshPlayerCache();
        lastRefresh = now;
    }

    int players = 0;

    for (const auto& player : g_cachedPlayers) {
        /* Filter based on current menu settings */
        if (player.isSleeping && !g_espShowSleepers) continue;
        if (player.isWounded && !g_espShowWounded) continue;
        if (player.distance > g_espMaxDist && g_espMaxDist > 0.f) continue;
        if (g_espVisCheck && !player.isVisible) continue;

        /* World to screen */
        Vec2 screenFeet, screenHead;
        bool feetOnScreen = RustSDK::WorldToScreen(
            player.position, g_ViewMatrix, g_ScreenW, g_ScreenH, screenFeet);
        bool headOnScreen = RustSDK::WorldToScreen(
            player.headPos, g_ViewMatrix, g_ScreenW, g_ScreenH, screenHead);

        if (!feetOnScreen && !headOnScreen) continue;

        /* Box dimensions */
        float boxH = fabsf(screenFeet.y - screenHead.y);
        float boxW = boxH * 0.45f;
        if (boxH < 2.f) continue;

        float cx = (screenFeet.x + screenHead.x) * 0.5f;

        /* Pick color */
        ImU32 color = COL_ENEMY;
        if (player.isSleeping)
            color = COL_SLEEPER;
        else if (player.isWounded)
            color = COL_WOUNDED;
        else if (player.teamID != 0 && player.teamID == g_LocalTeam)
            color = COL_TEAM;

        players++;

        /* ── Draw box ────────────────────────────────────── */
        if (g_espBoxes) {
            float x1 = cx - boxW * 0.5f;
            float y1 = screenHead.y;
            float x2 = cx + boxW * 0.5f;
            float y2 = screenFeet.y;

            draw->AddRect(ImVec2(x1 - 1, y1 - 1), ImVec2(x2 + 1, y2 + 1),
                          IM_COL32(0, 0, 0, 180), 0.f, 0, 1.f);
            draw->AddRect(ImVec2(x1, y1), ImVec2(x2, y2),
                          color, 0.f, 0, 1.5f);
        }

        /* ── Draw name ───────────────────────────────────── */
        float textY = screenHead.y - 16.f;
        if (g_espNames && !player.name.empty()) {
            char nameBuf[128] = {};
            WideCharToMultiByte(CP_UTF8, 0, player.name.c_str(), -1,
                                nameBuf, sizeof(nameBuf), nullptr, nullptr);

            ImVec2 textSize = ImGui::CalcTextSize(nameBuf);
            float tx = cx - textSize.x * 0.5f;

            draw->AddText(ImVec2(tx + 1, textY + 1),
                          IM_COL32(0, 0, 0, 200), nameBuf);
            draw->AddText(ImVec2(tx, textY), color, nameBuf);
            textY -= 14.f;
        }

        /* ── Draw distance ───────────────────────────────── */
        if (g_espDistance) {
            char distBuf[32];
            snprintf(distBuf, sizeof(distBuf), "%.0fm", player.distance);
            ImVec2 textSize = ImGui::CalcTextSize(distBuf);
            float tx = cx - textSize.x * 0.5f;
            draw->AddText(ImVec2(tx + 1, screenFeet.y + 3),
                          IM_COL32(0, 0, 0, 200), distBuf);
            draw->AddText(ImVec2(tx, screenFeet.y + 2),
                          COL_WHITE, distBuf);
        }

        /* ── Draw snapline ───────────────────────────────── */
        if (g_espSnaplines) {
            draw->AddLine(
                ImVec2((float)(g_ScreenW / 2), (float)g_ScreenH),
                ImVec2(screenFeet.x, screenFeet.y),
                COL_SNAP, 1.0f);
        }

        /* ── Health bar (left side) ──────────────────────── */
        if (g_espHealthBar && g_espBoxes) {
            float hpFrac = 1.0f;
            if (player.isWounded) hpFrac = 0.15f;
            if (player.isSleeping) hpFrac = 0.5f;

            float barX = cx - boxW * 0.5f - 5.f;
            float barTop = screenHead.y;
            float barBot = screenFeet.y;
            float barH = barBot - barTop;
            float filledH = barH * hpFrac;

            draw->AddRectFilled(ImVec2(barX - 2, barTop),
                                ImVec2(barX, barBot),
                                IM_COL32(0, 0, 0, 150));
            ImU32 hpColor = IM_COL32(
                (int)(255 * (1.f - hpFrac)),
                (int)(255 * hpFrac),
                0, 255);
            draw->AddRectFilled(ImVec2(barX - 2, barBot - filledH),
                                ImVec2(barX, barBot),
                                hpColor);
        }
    }

    g_PlayerCount = players;
}

/* ── Main ────────────────────────────────────────────────────────── */

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    (void)hInstance; (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;

    /* Allocate console for debug output */
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);

    printf("================================\n");
    printf("   NullkD - Rust External ESP\n");
    printf("================================\n\n");
    printf("[*] INSERT = Toggle Menu\n");
    printf("[*] END    = Exit\n\n");

    /* Connect to driver */
    printf("[*] Connecting to driver...\n");
    if (g_Driver.Init()) {
        printf("[+] Driver connected!\n");
    } else {
        printf("[!] Driver not connected. Load driver first.\n");
        printf("[!] Continuing anyway (menu will show disconnected state)\n");
    }

    /* Create SDK instance */
    g_SDK = new RustSDK(&g_Driver);

    /* Try to attach to Rust immediately */
    if (g_Driver.IsConnected()) {
        printf("[*] Looking for RustClient.exe...\n");
        if (g_SDK->Attach()) {
            printf("[+] Attached to Rust!\n");
        } else {
            printf("[!] Rust not running. Use menu to attach later.\n");
        }
    }

    /* Create overlay */
    printf("[*] Creating overlay window...\n");
    if (!CreateOverlayWindow()) {
        printf("[!] Failed to create overlay window\n");
        return 1;
    }
    printf("[+] Overlay window created (%dx%d)\n", g_ScreenW, g_ScreenH);

    /* Init DirectX 11 */
    printf("[*] Initializing DirectX 11...\n");
    if (!InitD3D11()) {
        printf("[!] Failed to initialize DirectX 11\n");
        return 1;
    }
    printf("[+] DirectX 11 initialized\n");

    /* Init ImGui */
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    /* Style */
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 6.0f;
    style.FrameRounding     = 4.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 4.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.Alpha             = 0.95f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]           = ImVec4(0.06f, 0.06f, 0.10f, 0.94f);
    colors[ImGuiCol_TitleBg]            = ImVec4(0.08f, 0.08f, 0.14f, 1.00f);
    colors[ImGuiCol_TitleBgActive]      = ImVec4(0.12f, 0.12f, 0.22f, 1.00f);
    colors[ImGuiCol_Tab]                = ImVec4(0.12f, 0.12f, 0.22f, 1.00f);
    colors[ImGuiCol_TabSelected]        = ImVec4(0.20f, 0.25f, 0.55f, 1.00f);
    colors[ImGuiCol_TabHovered]         = ImVec4(0.28f, 0.33f, 0.68f, 0.80f);
    colors[ImGuiCol_FrameBg]            = ImVec4(0.12f, 0.12f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]     = ImVec4(0.20f, 0.22f, 0.35f, 1.00f);
    colors[ImGuiCol_Button]             = ImVec4(0.20f, 0.25f, 0.50f, 1.00f);
    colors[ImGuiCol_ButtonHovered]      = ImVec4(0.28f, 0.33f, 0.65f, 1.00f);
    colors[ImGuiCol_ButtonActive]       = ImVec4(0.15f, 0.20f, 0.45f, 1.00f);
    colors[ImGuiCol_Header]             = ImVec4(0.20f, 0.25f, 0.50f, 0.55f);
    colors[ImGuiCol_HeaderHovered]      = ImVec4(0.26f, 0.30f, 0.60f, 0.80f);
    colors[ImGuiCol_HeaderActive]       = ImVec4(0.26f, 0.30f, 0.60f, 1.00f);
    colors[ImGuiCol_CheckMark]          = ImVec4(0.40f, 0.55f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]         = ImVec4(0.30f, 0.40f, 0.80f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]   = ImVec4(0.35f, 0.45f, 0.90f, 1.00f);

    /* ImGui platform/renderer backends */
    ImGui_ImplWin32_Init(g_hWnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dContext);

    printf("[+] ImGui initialized\n");
    printf("[*] Overlay running...\n\n");

    /* ── Main Loop ───────────────────────────────────────────────── */

    MSG msg = {};
    DWORD lastAttachTick = 0;

    while (g_Running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) g_Running = false;
        }
        if (!g_Running) break;

        /* Hotkeys */
        if (GetAsyncKeyState(VK_INSERT) & 1) {
            g_ShowMenu = !g_ShowMenu;
            SetClickThrough(!g_ShowMenu);
        }
        if (GetAsyncKeyState(VK_END) & 1) {
            g_Running = false;
            break;
        }

        /* Auto re-attach every 3 seconds if not attached */
        if (g_SDK && !g_SDK->IsAttached() && g_Driver.IsConnected()) {
            DWORD now = GetTickCount();
            if (now - lastAttachTick > 3000) {
                lastAttachTick = now;
                g_SDK->Attach();
            }
        }

        /* Begin frame */
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        /* Render */
        RenderMenu();
        RenderESP();

        /* End frame */
        ImGui::Render();

        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        g_pd3dContext->OMSetRenderTargets(1, &g_pRenderTargetView, nullptr);
        g_pd3dContext->ClearRenderTargetView(g_pRenderTargetView, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    /* ── Cleanup ─────────────────────────────────────────────────── */

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupD3D11();

    DestroyWindow(g_hWnd);
    UnregisterClassW(g_wc.lpszClassName, g_wc.hInstance);

    delete g_SDK;
    g_SDK = nullptr;

    if (f) fclose(f);
    FreeConsole();

    return 0;
}
