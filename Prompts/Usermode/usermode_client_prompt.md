# Usermode Client Architecture — Complete Build Prompt

> **Purpose**: This prompt contains everything needed to recreate the usermode client from scratch. It covers: driver communication, game offset tables, IL2CPP/Unity pointer decryption, GC handle resolution, entity iteration, player data reading, camera/view matrix, WorldToScreen projection, DirectX 11 overlay, ImGui menu, and ESP rendering.

> **Prerequisites**: Before using this prompt, the kernel driver must be built and loaded. See the separate driver prompt for kernel-side architecture. ImGui and MinHook libraries will be provided in the same project directory.

---

## 1. Project Structure

```
User/
├── User.sln                — Visual Studio solution
├── User.vcxproj            — Project file (Win32 app, NOT console)
├── main.cpp                — Entry point, overlay, ImGui menu, ESP rendering
├── driver_comm.h           — Kernel driver communication class
├── rust_sdk.h              — Game memory reader (SDK): decryption, entity iter, player data
├── rust_offsets.h          — All game offsets in namespaces
├── overlay.h               — Overlay window class (alternative, not used in main.cpp)
├── imgui/                  — ImGui library (provided by user)
│   ├── imgui.h / imgui.cpp
│   ├── imgui_internal.h
│   └── backends/
│       ├── imgui_impl_win32.h / .cpp
│       └── imgui_impl_dx11.h / .cpp
└── minhook/                — MinHook library (provided by user, if needed)
```

**Build type**: Windows Desktop Application (WinMain entry point, NOT console app)  
**Subsystem**: Windows (`/SUBSYSTEM:WINDOWS`)  
**Linked libs**: `d3d11.lib`, `dxgi.lib`, `dwmapi.lib`

---

## 2. Driver Communication ([driver_comm.h](

### 2.1 How It Works

The kernel driver hooks `NtQueryCompositionSurfaceStatistics` in `dxgkrnl.sys`. The usermode client calls this function with a `REQUEST_DATA` struct. If `magic == REQUEST_MAGIC`, the driver processes the command; otherwise it passes through to the original function.

### 2.2 Protocol

```cpp
#define REQUEST_MAGIC 0x44524B4E  // "DRKN"

typedef struct _REQUEST_DATA {
    unsigned int    magic;
    unsigned int    command;         // DRIVER_COMMAND enum
    unsigned __int64 pid;
    unsigned __int64 address;
    unsigned __int64 buffer;         // usermode buffer (cast to uint64)
    unsigned __int64 size;
    unsigned __int64 result;         // output
    unsigned int    protect;
    wchar_t         module_name[64];
} REQUEST_DATA;
```

### 2.3 DriverComm Class

```cpp
typedef NTSTATUS(NTAPI* fn_NtQueryCompositionSurfaceStatistics)(PVOID);

class DriverComm {
private:
    fn_NtQueryCompositionSurfaceStatistics pNtQuery = nullptr;
    bool connected = false;

    bool SendRequest(REQUEST_DATA* req) {
        if (!pNtQuery) return false;
        req->magic = REQUEST_MAGIC;
        __try { pNtQuery(req); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        return true;
    }

public:
    bool Init() {
        HMODULE hWin32u = LoadLibraryA("win32u.dll");
        pNtQuery = (fn_NtQueryCompositionSurfaceStatistics)
            GetProcAddress(hWin32u, "NtQueryCompositionSurfaceStatistics");
        // Ping to verify hook
        REQUEST_DATA req = {};
        req.command = CMD_PING;
        SendRequest(&req);
        connected = (req.result == 0x50544548 || req.result == 0x4B524E4C);
        return connected;
    }

    bool ReadMemory(DWORD pid, uintptr_t address, void* buffer, size_t size);
    bool WriteMemory(DWORD pid, uintptr_t address, void* buffer, size_t size);
    uintptr_t GetModuleBase(DWORD pid, const wchar_t* moduleName);
    uintptr_t AllocMemory(DWORD pid, size_t size, DWORD protect);
    void FreeMemory(DWORD pid, uintptr_t address);
    void ProtectMemory(DWORD pid, uintptr_t address, size_t size, DWORD protect);

    template<typename T> T Read(DWORD pid, uintptr_t address);
    template<typename T> bool Write(DWORD pid, uintptr_t address, const T& value);
};
```

**Key**: [Init()]( loads `win32u.dll` (NOT `dxgkrnl.sys` — the syscall routes through win32u → win32kbase → dxgkrnl in kernel). Ping response `0x50544548` = PTE hook active, `0x4B524E4C` = inline hook fallback.

---

## 3. Game Offsets ([rust_offsets.h](

All offsets are for **Rust patch 21886569** (Feb 2026). Organized in nested namespaces.

### 3.1 GameAssembly.dll RVA Globals

```cpp
namespace Offsets {
    // TypeInfo pointers (RVAs added to GameAssembly.dll base)
    inline constexpr uintptr_t BaseNetworkable_TypeInfo = 0xD65A0A8;
    inline constexpr uintptr_t MainCamera_TypeInfo      = 0xD676960;
    inline constexpr uintptr_t BaseViewModel_TypeInfo   = 0xD6B9878;
    inline constexpr uintptr_t TodSky_TypeInfo          = 0xD6C6278;
    inline constexpr uintptr_t GameManager_TypeInfo     = 0xD6BA370;
    inline constexpr uintptr_t il2cpp_get_handle        = 0xD985DD0;
}
```

### 3.2 Entity Traversal Chain

```cpp
namespace EntityChain {
    inline constexpr uint32_t static_fields    = 0xB8;  // TypeInfo → staticFields
    inline constexpr uint32_t client_entities   = 0x08;  // encrypted wrapper
    inline constexpr uint32_t object_dictionary = 0x20;  // plain ptr
    inline constexpr uint32_t content           = 0x10;  // entity array
    inline constexpr uint32_t size              = 0x18;  // entity count
}
```

### 3.3 Camera Chain

```cpp
namespace CameraChain {
    inline constexpr uint32_t static_fields = 0xB8;
    inline constexpr uint32_t instance      = 0x58;
    inline constexpr uint32_t buffer        = 0x10;
    inline constexpr uint32_t view_matrix   = 0x30C;
    inline constexpr uint32_t position      = 0x454;
}
```

### 3.4 Player Offsets

```cpp
namespace BasePlayer {
    inline constexpr uint32_t input         = 0x2F0;   // PlayerInput
    inline constexpr uint32_t currentTeam   = 0x4A0;
    inline constexpr uint32_t clActiveItem  = 0x4D0;   // EncryptedValue<ItemId>
    inline constexpr uint32_t playerFlags   = 0x5E8;
    inline constexpr uint32_t playerModel   = 0x5F0;
    inline constexpr uint32_t playerEyes    = 0x5F8;   // encrypted
    inline constexpr uint32_t displayName   = 0x620;
    inline constexpr uint32_t inventory     = 0x658;   // encrypted
    inline constexpr uint32_t movement      = 0x508;
}

namespace PlayerFlags {
    inline constexpr uint32_t IsAdmin      = (1 << 2);
    inline constexpr uint32_t Wounded      = (1 << 4);
    inline constexpr uint32_t IsSleeping   = (1 << 5);
    inline constexpr uint32_t IsConnected  = (1 << 8);
}

namespace PlayerModel {
    inline constexpr uint32_t position      = 0x1F8;
    inline constexpr uint32_t newVelocity   = 0x21C;
    inline constexpr uint32_t boxCollider   = 0x20;
    inline constexpr uint32_t skinnedMultiMesh = 0x378;
}

namespace BaseCombatEntity {
    inline constexpr uint32_t lifestate = 0x258;
}
```

### 3.5 Decryption Function RVAs

```cpp
namespace Decrypt {
    inline constexpr uintptr_t client_entities_fn = 0xCD3A30;
    inline constexpr uintptr_t entity_list_fn     = 0xBB4D00;
    inline constexpr uintptr_t player_eyes_fn     = 0xBD9740;
    inline constexpr uintptr_t player_inv_fn      = 0xBDAB80;
    inline constexpr uintptr_t cl_active_item_fn  = 0xDF220;
}
```

### 3.6 IL2CPP Internal Layouts

```cpp
namespace Il2CppString {
    inline constexpr uint32_t length = 0x10;   // int32 length
    inline constexpr uint32_t chars  = 0x14;   // wchar_t[] (UTF-16)
}

namespace Il2CppList {
    inline constexpr uint32_t items = 0x10;    // T[] _items
    inline constexpr uint32_t size  = 0x18;    // int _size
}

namespace Il2CppArray {
    inline constexpr uint32_t length = 0x18;
    inline constexpr uint32_t first  = 0x20;   // first element
}
```

### 3.7 Other Offsets (Item, TOD_Sky, GameManager, etc.)

```cpp
namespace PlayerEyes     { uint32_t bodyRotation = 0x50; }
namespace BaseEntity     { uint32_t model = 0xE8; uint32_t isVisible = 0x18; }
namespace Model          { uint32_t boneTransforms = 0x50; }
namespace PlayerInventory { uint32_t containerBelt = 0x78; }
namespace ItemContainer  { uint32_t itemList = 0x58; }
namespace WorldItem      { uint32_t item = 0x1C8; }
namespace Item           { uint32_t uid = 0x60; uint32_t itemDefinition = 0xD0; }
namespace ItemDefinition { uint32_t shortname = 0x28; uint32_t category = 0x58; }

namespace TodSky {
    uint32_t instance = 0x90; uint32_t night = 0x60; uint32_t cycle = 0x40;
}
namespace TOD_NightParameters {
    uint32_t lightIntensity = 0x50; uint32_t reflectionMultiplier = 0x64;
}
namespace TOD_CycleParameters { uint32_t hour = 0x10; }
namespace GameManager {
    uint32_t instance = 0x40; uint32_t prefabPoolCollection = 0x20;
}
```

---

## 4. Pointer Decryption ([rust_sdk.h](

### 4.1 Why Decrypt?

Rust (IL2CPP + EAC) encrypts key pointers to prevent external memory scanners from following pointer chains. The encryption is **per-build** — each game update changes the constants. The operations are simple arithmetic on each 32-bit half of the 64-bit value.

### 4.2 Generic Pattern

Every decrypt function operates on the **two 32-bit halves** independently:

```cpp
static uintptr_t DecryptXxx(uintptr_t encrypted_qword) {
    uint32_t* parts = (uint32_t*)&encrypted_qword;
    for (int i = 0; i < 2; i++) {
        uint32_t v = parts[i];
        // Apply sequence of ROL, ADD, XOR, SUB
        parts[i] = v;
    }
    return encrypted_qword;
}
```

### 4.3 Current Constants (Patch 21886569)

| Function | Operations |
|----------|-----------|
| **DecryptClientEntities** | ROL 12 → ADD 0x73400338 → ROL 9 |
| **DecryptEntityList** | ROL 20 → XOR 0xDA2510F8 → ROL 4 → XOR 0xFD0B1AB6 |
| **DecryptPlayerEyes** | ADD 0x5A59459F → ROL 1 → ADD 0x533DF48A → ROL 5 |
| **DecryptPlayerInventory** | SUB 0x600B999C → XOR 0xE017EC85 → ROL 6 |

### 4.4 How ROL Works

```cpp
// ROL (Rotate Left) by N bits for 32-bit value:
v = (v << N) | (v >> (32 - N));
```

### 4.5 Dynamic Constant Extraction

The SDK includes [DumpDecryptFunction()]( which reads actual game code bytes and disassembles arithmetic instructions to print the correct constants. This is essential when offsets change after a game update:

```
Scans for: XOR eax,imm32 (0x35), SUB eax,imm32 (0x2D), ADD eax,imm32 (0x05)
           Group1 0x81 (ADD/XOR/SUB rm32,imm32)
           Group2 0xC1 (ROL/ROR/SHL/SHR rm32,imm8)
```

---

## 5. GC Handle Resolution

### 5.1 Why GC Handles?

After decrypting a pointer, the result is NOT a direct pointer — it's a **GC handle** (garbage collector handle). IL2CPP uses a handle table to track managed objects. We need to resolve: `handle → object pointer`.

### 5.2 Handle Layout

```
uint32_t handle:
  - type  = handle & 7        (1=Weak, 2=Normal, 3=Pinned, 4=WeakTrack)
  - index = handle >> 3       (which slot in the objects array)
```

### 5.3 Type Array Structure

Found by disassembling `il2cpp_gchandle_get_target`:

```
Base address (in GameAssembly.dll .bss section):
  Each type has a 40-byte (5 qword) record:
    +0x00: bitmap pointer (allocation tracking)
    +0x08: objects array pointer (Il2CppObject**)  ← this is what we need
    +0x10: capacity
    +0x18: metadata
    +0x20: metadata

Record address = base + (type - 1) * 40
Object pointer = record.objects_array[index]
```

### 5.4 Finding the GC Handle Table

Load `GameAssembly.dll` from disk (`LoadLibraryExW` with `LOAD_LIBRARY_AS_IMAGE_RESOURCE`), parse PE exports to find `il2cpp_gchandle_get_target` RVA, read its actual code from process memory, follow any JMP thunks (`0xE9`), then parse all RIP-relative `MOV` instructions (`48 8B 05` / `4C 8B 05` with ModRM `0x05`) to find globals in `.bss`. Each global is checked — if it looks like a bitmap (values > 0x7FFFFFFFFFFF), save as `bitmapGlobalAddr`; if it points to an array of 8-byte aligned valid pointers, that's the `gcHandleTable`.

### 5.5 Pointer Validation Helpers

```cpp
// Valid userspace pointer (above 68GB to exclude GC handle range)
static bool IsValidPtr(uintptr_t p) {
    return p > 0x10000000000ULL && p < 0x7FFFFFFFFFFF;
}

// 8-byte aligned managed object
static bool IsAlignedPtr(uintptr_t p) {
    return IsValidPtr(p) && (p & 7) == 0;
}
```

### 5.6 The Decrypt-and-Resolve Pipeline

Every encrypted field follows the same pattern:

```
1. Read wrapper pointer from entity + fieldOffset
2. Read 8 bytes of encrypted data from wrapper + 0x18
3. Apply field-specific decrypt function (ROL/XOR/ADD/SUB)
4. The result is a 32-bit GC handle
5. Resolve: objects_array[handle >> 3] → actual object pointer
```

```cpp
uintptr_t DecryptAndResolve(uintptr_t wrapperAddr, DecryptFn decrypt) {
    uintptr_t encrypted = Read<uintptr_t>(wrapperAddr + 0x18);
    uintptr_t decrypted = decrypt(encrypted);
    uint32_t handle = (uint32_t)(decrypted & 0xFFFFFFFF);
    return ResolveGCHandle(handle);
}
```

---

## 6. Entity Iteration

### 6.1 The Full Chain

```
GameAssembly + BaseNetworkable_TypeInfo  →  TypeInfo ptr
TypeInfo + 0xB8                          →  staticFields
staticFields + 0x08                      →  wrapper1 (encrypted)
  DecryptAndResolve(wrapper1, DecryptClientEntities)  →  clientEntities
clientEntities + 0x10                    →  wrapper2 (encrypted)
  DecryptAndResolve(wrapper2, DecryptEntityList)      →  entityList
entityList + 0x20                        →  objectDictionary
objDict + 0x10                           →  content (Il2CppArray of entity ptrs)
objDict + 0x18                           →  size (entity count)
```

### 6.2 Reading Entities

```cpp
uintptr_t GetEntity(int index) {
    return Read<uintptr_t>(entityBuffer + 0x20 + index * 8);
    // 0x20 = Il2CppArray::first (skip array header)
}
```

### 6.3 Entity Count Limits

Sanity check: `0 < count < 50000`. Typical server has 2000-8000 entities.

### 6.4 Entity Type Check (IL2CPP Class Name)

```
entity + 0x00 = Il2CppClass* klass
klass  + 0x10 = const char* name
```

Player class names: `"BasePlayer"`, `"NPCPlayer"`, `"ScientistNPC"`, `"HTNPlayer"`, `"NPCMurderer"`, `"HumanNPC"`, `"GingerbreadNPC"`

---

## 7. Camera & View Matrix

### 7.1 Chain

```
GameAssembly + MainCamera_TypeInfo  →  TypeInfo
TypeInfo + 0xB8   →  staticFields
static + 0x58     →  MainCamera instance
instance + 0x10   →  camera buffer
buffer + 0x30C    →  ViewMatrix (4×4 float = 64 bytes)
buffer + 0x454    →  Camera position (Vec3 = 12 bytes)
```

### 7.2 WorldToScreen

Standard FPS WorldToScreen using row-major 4×4 matrix:

```cpp
bool WorldToScreen(const Vec3& world, const ViewMatrix& vm,
                   int screenW, int screenH, Vec2& out)
{
    float w = vm.m[0][3] * world.x +
              vm.m[1][3] * world.y +
              vm.m[2][3] * world.z +
              vm.m[3][3];
    if (w < 0.001f) return false;  // Behind camera

    float invW = 1.0f / w;

    float sx = vm.m[0][0] * world.x +
               vm.m[1][0] * world.y +
               vm.m[2][0] * world.z +
               vm.m[3][0];

    float sy = vm.m[0][1] * world.x +
               vm.m[1][1] * world.y +
               vm.m[2][1] * world.z +
               vm.m[3][1];

    out.x = (screenW * 0.5f) + (screenW * 0.5f) * sx * invW;
    out.y = (screenH * 0.5f) - (screenH * 0.5f) * sy * invW;

    return (out.x >= -50.f && out.x <= screenW + 50.f &&
            out.y >= -50.f && out.y <= screenH + 50.f);
}
```

---

## 8. Player Data Reading

### 8.1 PlayerData Struct

```cpp
struct PlayerData {
    uintptr_t   address;
    Vec3        position;
    Vec3        headPos;        // position + Vec3(0, 1.6, 0)
    std::wstring name;
    uint64_t    teamID;
    uint32_t    flags;
    uint32_t    lifestate;      // 0 = alive
    bool        isVisible;
    bool        isSleeping;
    bool        isWounded;
    float       distance;
};
```

### 8.2 Reading a Player

```cpp
bool ReadPlayer(uintptr_t entity, PlayerData& out) {
    // 1. PlayerModel → position
    uintptr_t playerModel = Read<uintptr_t>(entity + 0x5F0);
    out.position = Read<Vec3>(playerModel + 0x1F8);
    if (position is zero) return false;

    // 2. Head = position + 1.6m up
    out.headPos = Vec3(pos.x, pos.y + 1.6f, pos.z);

    // 3. Name (IL2CPP System.String pointer)
    uintptr_t namePtr = Read<uintptr_t>(entity + 0x620);
    out.name = ReadString(namePtr);

    // 4. Team ID
    out.teamID = Read<uint64_t>(entity + 0x4A0);

    // 5. Flags (sleeping, wounded, connected)
    out.flags = Read<uint32_t>(entity + 0x5E8);
    out.isSleeping = (flags & (1 << 5)) != 0;
    out.isWounded  = (flags & (1 << 4)) != 0;

    // 6. Lifestate (0 = alive)
    out.lifestate = Read<uint32_t>(entity + 0x258);
}
```

### 8.3 Reading IL2CPP Strings (UTF-16)

```cpp
std::wstring ReadString(uintptr_t strPtr, int maxChars = 64) {
    int len = Read<int>(strPtr + 0x10);   // Il2CppString::length
    if (len <= 0 || len > maxChars) len = maxChars;
    std::wstring result(len, L'\0');
    ReadRaw(strPtr + 0x14, result.data(), len * sizeof(wchar_t));
    return result;
}
```

---

## 9. Process Attachment

### 9.1 Find Rust Process

```cpp
bool Attach() {
    // 1. CreateToolhelp32Snapshot → find "RustClient.exe" → get PID
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe = { sizeof(pe) };
    Process32FirstW(snap, &pe);
    do {
        if (_wcsicmp(pe.szExeFile, L"RustClient.exe") == 0) {
            pid = pe.th32ProcessID;
            break;
        }
    } while (Process32NextW(snap, &pe));

    // 2. Get GameAssembly.dll base via driver
    gameAssembly = drv->GetModuleBase(pid, L"GameAssembly.dll");

    // 3. Find GC handle table (Methods A/B/C)
    FindGCHandleTable();

    // 4. Dump decrypt functions to console for verification
    AnalyzeDecryptFunctions();
}
```

---

## 10. Overlay Window

### 10.1 Window Flags

```cpp
g_wc.lpszClassName = L"MSCTFIME UI";  // Mimics Windows IME class

CreateWindowExW(
    WS_EX_TOPMOST |       // On top of game
    WS_EX_TRANSPARENT |   // Click-through
    WS_EX_LAYERED |       // For transparency
    WS_EX_TOOLWINDOW |    // Hidden from taskbar/alt-tab
    WS_EX_NOACTIVATE,     // Never steal focus
    ..., L"", WS_POPUP, 0, 0, screenW, screenH, ...
);

SetLayeredWindowAttributes(hWnd, RGB(0,0,0), 0, LWA_COLORKEY);
MARGINS m = {-1,-1,-1,-1};
DwmExtendFrameIntoClientArea(hWnd, &m);
SetWindowDisplayAffinity(g_hWnd, WDA_EXCLUDEFROMCAPTURE);  // default: hidden
```

### 10.2 Click-Through Toggle (INSERT Key)

```cpp
void SetClickThrough(bool clickThrough) {
    LONG_PTR ex = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
    if (clickThrough)
        ex |= (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
    else
        ex &= ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
    SetWindowLongPtrW(hWnd, GWL_EXSTYLE, ex);
    if (!clickThrough) { SetForegroundWindow(hWnd); SetFocus(hWnd); }
}
```

---

## 11. DirectX 11 Setup

```cpp
DXGI_SWAP_CHAIN_DESC sd = {};
sd.BufferCount  = 2;
sd.BufferDesc   = { screenW, screenH, {0,1}, DXGI_FORMAT_R8G8B8A8_UNORM };
sd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
sd.OutputWindow = hWnd;
sd.SampleDesc   = { 1, 0 };
sd.Windowed     = TRUE;
sd.SwapEffect   = DXGI_SWAP_EFFECT_DISCARD;
sd.Flags        = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
    nullptr, 0, D3D11_SDK_VERSION,
    &sd, &pSwapChain, &pDevice, &featureLevel, &pContext);

// Render target from back buffer
pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &pRTV);
```

---

## 12. ImGui Setup & Theme

### 12.1 Init

```cpp
IMGUI_CHECKVERSION();
ImGui::CreateContext();
ImGuiIO& io = ImGui::GetIO();
io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
io.IniFilename = nullptr;  // Don't save layout

ImGui_ImplWin32_Init(hWnd);
ImGui_ImplDX11_Init(pDevice, pContext);
```

### 12.2 Custom Dark Blue/Purple Theme

```cpp
ImGui::StyleColorsDark();
ImGuiStyle& s = ImGui::GetStyle();
s.WindowRounding = 6.0f;  s.FrameRounding = 4.0f;
s.GrabRounding   = 4.0f;  s.TabRounding   = 4.0f;
s.WindowBorderSize = 1.0f; s.Alpha = 0.95f;

ImVec4* c = s.Colors;
c[ImGuiCol_WindowBg]        = {0.06f, 0.06f, 0.10f, 0.94f};
c[ImGuiCol_TitleBg]         = {0.08f, 0.08f, 0.14f, 1.00f};
c[ImGuiCol_TitleBgActive]   = {0.12f, 0.12f, 0.22f, 1.00f};
c[ImGuiCol_Tab]             = {0.12f, 0.12f, 0.22f, 1.00f};
c[ImGuiCol_TabSelected]     = {0.20f, 0.25f, 0.55f, 1.00f};
c[ImGuiCol_TabHovered]      = {0.28f, 0.33f, 0.68f, 0.80f};
c[ImGuiCol_FrameBg]         = {0.12f, 0.12f, 0.20f, 1.00f};
c[ImGuiCol_FrameBgHovered]  = {0.20f, 0.22f, 0.35f, 1.00f};
c[ImGuiCol_Button]          = {0.20f, 0.25f, 0.50f, 1.00f};
c[ImGuiCol_ButtonHovered]   = {0.28f, 0.33f, 0.65f, 1.00f};
c[ImGuiCol_ButtonActive]    = {0.15f, 0.20f, 0.45f, 1.00f};
c[ImGuiCol_Header]          = {0.20f, 0.25f, 0.50f, 0.55f};
c[ImGuiCol_HeaderHovered]   = {0.26f, 0.30f, 0.60f, 0.80f};
c[ImGuiCol_HeaderActive]    = {0.26f, 0.30f, 0.60f, 1.00f};
c[ImGuiCol_CheckMark]       = {0.40f, 0.55f, 1.00f, 1.00f};
c[ImGuiCol_SliderGrab]      = {0.30f, 0.40f, 0.80f, 1.00f};
c[ImGuiCol_SliderGrabActive]= {0.35f, 0.45f, 0.90f, 1.00f};
```

### 12.3 WndProc

```cpp
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    switch (msg) {
        case WM_SIZE:
            if (pDevice && wParam != SIZE_MINIMIZED) {
                CleanupRenderTarget();
                pSwapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam),
                    DXGI_FORMAT_UNKNOWN, 0);
                CreateRenderTarget();
            }
            return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
```

---

## 13. ImGui Menu

### 13.1 Layout

Tab bar with ESP / Aim / Misc tabs:

```
┌──────────────────────────────────────┐
│ NullkD - Rust ESP                 [x]│
├──────────────────────────────────────┤
│ [+] Driver Connected | PID: 1234     │
├────────┬────────┬────────────────────┤
│  ESP   │  Aim   │  Misc             │
├────────┴────────┴────────────────────┤
│ [✓] Enable ESP                       │
│ [✓] Boxes    [✓] Names              │
│ [✓] Distance [ ] Snaplines          │
│ [ ] Sleepers [✓] Wounded            │
│ Max Distance: [=====] 1000m          │
├────────┬────────┬────────────────────┤
│  ESP   │  Aim   │  Misc             │
├────────┴────────┴────────────────────┤
│ Overlay Mode: [Hidden from capture]  │
│   ← toggles WDA_EXCLUDEFROMCAPTURE   │
│ [Exit (END)]                         │
└──────────────────────────────────────┘
```

### 13.2 ESP Config Variables

```cpp
bool  g_espEnabled      = true;
bool  g_espBoxes        = true;
bool  g_espNames        = true;
bool  g_espDistance      = true;
bool  g_espSnaplines    = false;
bool  g_espShowSleepers = false;
bool  g_espShowWounded  = true;
float g_espMaxDist      = 1000.0f;
bool  g_overlayHidden   = true;   // true = hidden from capture/recording
```

### 13.3 Colors

| State | Color | ImU32 |
|-------|-------|-------|
| Enemy | Red | `IM_COL32(255, 60, 60, 255)` |
| Teammate | Green | `IM_COL32(60, 255, 60, 255)` |
| Sleeper | Gray | `IM_COL32(160, 160, 160, 180)` |
| Wounded | Orange | `IM_COL32(255, 180, 0, 255)` |
| Snapline | White | `IM_COL32(255, 255, 255, 80)` |

### 13.4 Hotkeys

| Key | Action |
|-----|--------|
| INSERT | Toggle menu + click-through |
| END | Exit application |

---

## 14. ESP Rendering

### 14.1 Draw List

All ESP drawing uses `ImGui::GetForegroundDrawList()` — always renders on top of the menu.

### 14.2 Frame Flow

```
1. Check g_espEnabled && SDK attached
2. Get view matrix from camera chain
3. Get camera position for distance calc
4. Refresh entity list (throttled: every 500ms, NOT every frame)
5. Loop all entities:
   a. Skip non-players (class name check)
   b. ReadPlayer() → PlayerData
   c. Skip dead (lifestate != 0)
   d. Skip sleepers/wounded if disabled
   e. Calculate distance from camera
   f. Skip if > maxDist
   g. WorldToScreen for feet + head
   i. Calculate box dimensions: height = |feetY - headY|, width = height * 0.45
   j. Pick color based on state (sleeping/wounded/teammate/enemy)
   k. Draw elements
```

### 14.3 Drawing Elements

**Boxes** (2D cornered):
```cpp
// Black outline
draw->AddRect({x1-1, y1-1}, {x2+1, y2+1}, IM_COL32(0,0,0,180), 0, 0, 1.f);
// Colored inner
draw->AddRect({x1, y1}, {x2, y2}, color, 0, 0, 1.5f);
```

**Names** (centered above head, with shadow):
```cpp
// Convert wstring → UTF-8 via WideCharToMultiByte
ImVec2 sz = ImGui::CalcTextSize(nameBuf);
float tx = cx - sz.x * 0.5f;
draw->AddText({tx+1, textY+1}, IM_COL32(0,0,0,200), nameBuf);  // shadow
draw->AddText({tx, textY}, color, nameBuf);
```

**Distance** (centered below feet):
```cpp
snprintf(distBuf, sizeof(distBuf), "%.0fm", player.distance);
// Centered below screenFeet.y + 2
```

**Snaplines** (screen bottom center → feet):
```cpp
draw->AddLine({screenW/2, screenH}, {screenFeet.x, screenFeet.y}, COL_SNAP, 1.f);
```

---

## 15. Main Loop ([WinMain]()

```
1.  AllocConsole() + freopen stdout
2.  g_Driver.Init() — connect to kernel driver
3.  g_SDK = new RustSDK(&g_Driver)
4.  If connected: g_SDK->Attach() — find Rust, get GameAssembly base
5.  CreateOverlayWindow()
6.  InitD3D11()
7.  ImGui: CreateContext, StyleColorsDark, custom theme, ImplWin32_Init, ImplDX11_Init
8.  Main loop:
    a. PeekMessageW / TranslateMessage / DispatchMessageW
    b. Hotkeys: INSERT (toggle menu), END (exit)
    c. Auto re-attach every 3s if not attached
    d. ImGui_ImplDX11_NewFrame + ImGui_ImplWin32_NewFrame + ImGui::NewFrame
    e. RenderMenu()
    f. RenderESP()
    g. ImGui::Render()
    h. ClearRenderTargetView(rgba 0,0,0,0) — transparent
    i. ImGui_ImplDX11_RenderDrawData
    j. Present(1, 0) — vsync ON
9.  Cleanup:
    ImGui_ImplDX11_Shutdown, ImGui_ImplWin32_Shutdown, DestroyContext
    Release D3D11 objects
    DestroyWindow, UnregisterClass
    delete g_SDK
    fclose, FreeConsole
```

---

## 16. Key Design Decisions & Gotchas

| Topic | Detail |
|-------|--------|
| **Entity refresh rate** | 500ms throttle, not every frame — entities don't spawn/despawn fast enough to justify per-frame reads |
| **Head position** | Approximated as `position + Vec3(0, 1.6, 0)` — not reading bone transforms (too many reads per frame) |
| **HP value** | Not directly read — approximated from flags (wounded = 15%, sleeping = 50%) |
| **Team detection** | Compare `player.teamID` with `g_LocalTeam`. Local team cached, team color goes green |
| **String encoding** | Rust uses IL2CPP strings (UTF-16). [ReadString()](file:///C:/Users/MSZ/source/repos/Krnld/driver/User/rust_sdk.h#198-208) reads from `strPtr+0x14`. Must `WideCharToMultiByte` for ImGui display (AddText uses UTF-8) |
| **Box sizing** | Width = Height × 0.45 (standard humanoid aspect ratio). Skip if height < 2px |
| **Pointer validation** | Floor at 68GB (`0x10000000000`) to exclude GC handles which are small integers. All real game pointers are well above this |
| **Debug output** | Verbose logging every 300 frames (entity chain), every 3000 frames (ESP). Controlled by [dbgVerbose()](file:///C:/Users/MSZ/source/repos/Krnld/Krnld/driver/User/rust_sdk.h#162-163) |
| **WndProc order** | ImGui handler MUST be called first. If it returns true, message was consumed |
| **Console** | `AllocConsole()` + `freopen_s` for debug printf. Closed on exit |
