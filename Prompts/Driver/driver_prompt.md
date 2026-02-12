# Kernel Driver Architecture — Complete Build Prompt

> **Purpose**: This prompt contains everything needed to recreate the kernel driver from scratch. It covers every subsystem: communication hook, physical memory R/W, return address spoofing, and trace cleaning. All patterns, byte sequences, structures, and design decisions are documented. See the separate usermode client prompt for the ESP overlay.

---

## 1. Project Overview

**Type**: External kernel-mode driver for game memory R/W  
**Loader**: kdmapper (Intel iqvw64e.sys vulnerable driver)  
**Anti-cheat target**: EAC (Easy Anti-Cheat)  
**OS**: Windows 10/11 x64 (22H2+)  

### Architecture Flow

```
kdmapper loads driver.sys → DriverEntry()
  → Hook::Install() — PTE hook on dxgkrnl.sys
  → CleanAllTraces() — PiDDB, MmUnloadedDrivers, DriverObject hiding
  → Driver is now invisible, communicates via hooked function

Usermode client:
  → LoadLibrary("win32u.dll")
  → GetProcAddress("NtQueryCompositionSurfaceStatistics")
  → Call with REQUEST_DATA struct (magic = 0x44524B4E "DRKN")
  → Hooked function dispatches to our handler
```

---

## 2. File Structure

```
driver/
├── driver.cpp          — DriverEntry, calls Hook::Install then CleanAllTraces
├── driver.h            — Just includes definitions.h + memory.h
├── definitions.h       — Kernel imports, PEB/LDR structs, undocumented APIs
├── shared.h            — REQUEST_DATA struct + command IDs (shared with usermode)
├── hook.h              — Hook::Install / Hook::Handler prototypes
├── hook.cpp            — PTE hook installation + command dispatcher
├── pte_hook.h          — PTE-based hook engine (MiGetPteAddress, page remapping)
├── physical_memory.h   — CR3 caching, 4-level page walk, MmCopyMemory R/W
├── spoof_call.h        — Return address spoofing via ntoskrnl gadget
├── cleaner.h           — PiDDB, MmUnloadedDrivers, DriverObject cleaning
├── memory.h            — Prototypes for memory.cpp
└── memory.cpp          — MmCopyVirtualMemory R/W, PEB walker, alloc/free/protect
```

---


This header is included by BOTH kernel and usermode. It defines the communication protocol.

### Command IDs

```cpp
typedef enum _DRIVER_COMMAND {
    CMD_NONE          = 0,
    CMD_READ          = 1,    // Physical memory read (CR3 page walk)
    CMD_WRITE         = 2,    // Physical memory write (KeStackAttach)
    CMD_MODULE_BASE   = 3,    // Get module base via PEB walk
    CMD_ALLOC         = 4,    // ZwAllocateVirtualMemory in target
    CMD_FREE          = 5,    // ZwFreeVirtualMemory in target
    CMD_PROTECT       = 6,    // ZwProtectVirtualMemory in target
    CMD_READ64        = 7,    // Virtual read (MmCopyVirtualMemory fallback)
    CMD_WRITE64       = 8,    // Virtual write (MmCopyVirtualMemory fallback)
    CMD_PING          = 99,   // Connection check
    CMD_VERIFY_PTE    = 100,  // Debug: return PTE hook state
    CMD_VERIFY_SPOOF  = 101,  // Debug: return spoof call state
} DRIVER_COMMAND;
```

### Request Structure

```cpp
#define REQUEST_MAGIC 0x44524B4E  // "DRKN"

typedef struct _REQUEST_DATA {
    unsigned int    magic;           // Must be REQUEST_MAGIC
    unsigned int    command;         // DRIVER_COMMAND enum
    unsigned __int64 pid;           // Target process ID
    unsigned __int64 address;       // Virtual address for R/W
    unsigned __int64 buffer;        // Usermode buffer address
    unsigned __int64 size;          // Bytes to R/W
    unsigned __int64 result;        // Output: module base, alloc base, etc
    unsigned int    protect;        // Memory protection flags
    wchar_t         module_name[64]; // Module name for CMD_MODULE_BASE
} REQUEST_DATA, *PREQUEST_DATA;
```

**Key design**: The `magic` field distinguishes our calls from legitimate dxgkrnl calls. If `magic != REQUEST_MAGIC`, the handler returns `STATUS_SUCCESS` immediately (pass-through to original).

---


```cpp
extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    // 1. Install dxgkrnl hook for communication
    if (!Hook::Install(&Hook::Handler))
        return STATUS_UNSUCCESSFUL;

    // 2. Clean ALL traces and hide driver
    CleanAllTraces(DriverObject);

    return STATUS_SUCCESS;
}
```

**Order matters**: Hook first (needs dxgkrnl export resolution), then clean (removes our own traces from PiDDB, MmUnloadedDrivers, erases PE headers, NULLs DriverObject).

---


### 5.1 Hook Target

**Function**: `NtQueryCompositionSurfaceStatistics`  
**Module**: `dxgkrnl.sys`  
**Why**: This function is exported by dxgkrnl and callable from usermode via `win32u.dll`. It takes a single `PVOID` parameter — perfect for passing our `REQUEST_DATA` struct.

### 5.2 Hook Installation — PTE Method (Primary)

The PTE hook is the **stealth** method. It remaps the virtual-to-physical mapping so:
- The **original physical page** stays clean (what EAC integrity checks read)
- A **copy of the page** with our trampoline is what the CPU actually executes

**Step-by-step**:

1. **Find `MiGetPteAddress`** in ntoskrnl via pattern scan:
   ```
   Pattern (31 bytes):
   48 C1 E9 09              shr rcx, 9
   48 B8 [8 bytes MASK]     mov rax, <MASK>
   48 23 C8                 and rcx, rax
   48 B8 [8 bytes PTE_BASE] mov rax, <PTE_BASE>
   48 03 C1                 add rax, rcx
   C3                       ret
   ```
   This gives us reliable PTE resolution without hardcoding PTE_BASE.

2. **Check PDE for large page**: If the target is on a 2MB large page, split it into 512 × 4KB pages first:
   - Allocate contiguous page for new page table entries
   - Fill 512 PTEs pointing to consecutive 4KB frames of the original 2MB page
   - Atomically swap PDE to point to new page table (clear LargePage bit)
   - Invalidate TLB for all 512 pages

3. **Allocate new physical page**: `MmAllocateContiguousMemorySpecifyCache`

4. **Copy original page**: Map original physical page via `MmMapIoSpace`, `RtlCopyMemory` to new page

5. **Apply trampoline** at the hook offset within the copy:
   ```asm
   48 B8 [8-byte handler addr]    mov rax, <handlerAddr>
   FF E0                          jmp rax
   ```

6. **Swap PFN in PTE**: Atomically change `pte->PageFrameNumber` to point to our new page using `InterlockedExchange64`, then `__invlpg` to flush TLB.

### 5.3 Hook Installation — Inline Patch (Fallback)

If PTE hook fails (unusual Windows version, PTE pattern not found), falls back to direct code patching:

```cpp
BYTE patch[12] = { 0 };
patch[0] = 0x48;  // REX.W
patch[1] = 0xB8;  // MOV RAX, imm64
memcpy(&patch[2], &handlerAddr, 8);
patch[10] = 0xFF; // JMP RAX
patch[11] = 0xE0;
WriteReadOnlyMemory(hookTarget, patch, 12);
```

Uses MDL-based write: `IoAllocateMdl` → `MmProbeAndLockPages` → `MmMapLockedPagesSpecifyCache` → `MmProtectMdlSystemAddress(PAGE_READWRITE)` → `memcpy`.

### 5.4 PTE Hook State

```cpp
typedef struct _PTE_HOOK_STATE {
    PVOID       targetVA;       // Hooked virtual address
    PPTE_ENTRY  pteAddress;     // Pointer to the PTE
    ULONG64     originalPfn;    // Original physical frame number
    ULONG64     newPfn;         // Our page's physical frame number
    PVOID       newPageVA;      // Virtual address of our allocated page
    PHYSICAL_ADDRESS newPagePA; // Physical address of our page
    BOOLEAN     active;         // Is hook active?
} PTE_HOOK_STATE;
```

### 5.5 Command Dispatcher (`Hook::Handler`)

The handler checks `magic == REQUEST_MAGIC`, then switches on `command`:

| Command | Handler | Notes |
|---------|---------|-------|
| `CMD_READ` | `PhysicalReadProcessMemory()` | CR3 page walk, EAC-safe |
| `CMD_WRITE` | `PhysicalWriteProcessMemory()` | KeStackAttach + direct copy |
| `CMD_READ64` | `myReadProcessMemory()` | MmCopyVirtualMemory fallback |
| `CMD_WRITE64` | `myWriteProcessMemory()` | MmCopyVirtualMemory fallback |
| `CMD_MODULE_BASE` | `GetProcessModuleBase()` | PEB LDR walk |
| `CMD_ALLOC` | `AllocateVirtualMemory()` | ZwAllocateVirtualMemory |
| `CMD_FREE` | `FreeVirtualMemory()` | ZwFreeVirtualMemory |
| `CMD_PROTECT` | `ProtectVirtualMemory()` | ZwProtectVirtualMemory |
| `CMD_PING` | Returns `0x50544548` or `0x4B524E4C` | PTE hook vs fallback indicator |
| `CMD_VERIFY_PTE` | Returns full PTE state to usermode | Debug: original vs new PFN, bytes |
| `CMD_VERIFY_SPOOF` | Returns gadget + stub addresses | Debug |

---


### 6.1 Why Physical R/W?

EAC monitors `MmCopyVirtualMemory` calls — the standard way to read another process's memory. Physical memory R/W bypasses this entirely by:
1. Getting the target process's CR3 (page table root)
2. Walking the 4-level page table to translate virtual → physical
3. Reading physical memory directly via `MmCopyMemory(MM_COPY_MEMORY_PHYSICAL)`

### 6.2 CR3 Acquisition

**Problem**: `EPROCESS+0x28` (DirectoryTableBase) is encrypted/stripped by EAC in recent versions.

**Solution**: Attach to the process and read CR3 from hardware:
```cpp
KAPC_STATE apc;
KeStackAttachProcess(process, &apc);
ULONG64 cr3 = __readcr3();  // Read from CPU register directly
KeUnstackDetachProcess(&apc);
```

### 6.3 CR3 Caching

EAC may trash CR3 early during process creation. The driver uses aggressive caching:

```cpp
typedef struct _CR3_CACHE_ENTRY {
    volatile ULONG64 cr3;
    volatile HANDLE  pid;
    volatile ULONG   callCount;
    volatile BOOLEAN validated;
} CR3_CACHE_ENTRY;

#define CR3_CACHE_THRESHOLD  500  // Don't trust cache until 500+ calls
```

**Validation**: Reads the first PML4 entry via `MmCopyMemory`. A valid CR3 should have at least one present PML4 entry (usermode pages exist).

### 6.4 4-Level Page Table Walk

```cpp
static ULONG64 TranslateVirtualAddress(ULONG64 cr3, ULONG64 va)
{
    ULONG64 pml4_idx = (va >> 39) & 0x1FF;   // Bits 39-47
    ULONG64 pdpt_idx = (va >> 30) & 0x1FF;   // Bits 30-38
    ULONG64 pd_idx   = (va >> 21) & 0x1FF;   // Bits 21-29
    ULONG64 pt_idx   = (va >> 12) & 0x1FF;   // Bits 12-20
    ULONG64 offset   = va & 0xFFF;            // Bits 0-11

    // PML4 → PDPT → PD → PT → Physical
    // Each level: read 8 bytes at (parentPhys & MASK) + index * 8
    // Check Present bit, handle 1GB/2MB large pages
}
```

**Large page handling**:
- **1GB page** (PDPT level): [(pte & 0xFFFFC0000000) + (va & 0x3FFFFFFF)](
- **2MB page** (PD level): [(pte & 0xFFFFFE00000) + (va & 0x1FFFFF)](
- **4KB page** (PT level): [(pte & MASK) + offset](

### 6.5 Read Implementation

```
PhysicalReadProcessMemory(pid, virtualAddress, userBuffer, size):
  1. GetProcessCR3(pid) → cr3 (cached after 500 calls)
  2. Allocate kernel intermediate buffer (ExAllocatePoolWithTag)
  3. Loop over page boundaries:
     a. TranslateVirtualAddress(cr3, currentVA) → physAddr
     b. ReadPhysicalAddress(physAddr, kernelBuf + offset, chunkSize)
        → MmCopyMemory(buffer, addr, size, MM_COPY_MEMORY_PHYSICAL, &bytesRead)
  4. Copy kernel buffer → usermode buffer (RtlCopyMemory with __try/__except)
  5. Free kernel buffer
```

**CRITICAL**: `MmCopyMemory` writes to a kernel buffer, NOT directly to usermode. You MUST use an intermediate kernel buffer then copy to usermode.

### 6.6 Write Implementation

Writes use `KeStackAttachProcess` + direct `RtlCopyMemory`, NOT physical write. Reasons:
- Demand-zero pages have no physical backing yet
- `MmMapIoSpace` is for device MMIO, not regular RAM
- EAC primarily monitors cross-process **READS**, writes are less scrutinized
- `KeStackAttachProcess` + direct copy doesn't trigger `MmCopyVirtualMemory` hooks

```
PhysicalWriteProcessMemory(pid, virtualAddress, userBuffer, size):
  1. Copy usermode buffer → kernel buffer (with __try/__except)
  2. PsLookupProcessByProcessId → process (via SpoofCall if available)
  3. KeStackAttachProcess(process, &apc)
  4. RtlCopyMemory(virtualAddress, kernelBuf, size) with __try/__except
  5. KeUnstackDetachProcess(&apc)
  6. ObfDereferenceObject(process)
```

---

## 7. Return Address Spoofing ([spoof_call.h](

### 7.1 Problem

When our manually-mapped driver calls kernel APIs (e.g., `PsLookupProcessByProcessId`), the return address on the stack points to our code — which is in unmapped/non-module memory. EAC stack walks detect this.

### 7.2 Solution

Route calls through a gadget in `ntoskrnl.exe` so the callee sees a return address inside a legitimate module.

### 7.3 Gadget

```
Pattern: 48 83 C4 28 C3
  add rsp, 0x28
  ret
```

This is a very common function epilogue in ntoskrnl. Found by scanning executable sections.

### 7.4 Shellcode Stub (52 bytes)

Called as: `stub(targetFn, arg1, arg2, arg3, arg4)`

```asm
41 5B                 pop r11              ; save real return address
48 8B C1              mov rax, rcx         ; rax = target function
48 8B CA              mov rcx, rdx         ; shift arg1 → rcx
49 8B D0              mov rdx, r8          ; shift arg2 → rdx
4D 8B C1              mov r8, r9           ; shift arg3 → r8
4C 8B 4C 24 20        mov r9, [rsp+0x20]   ; shift arg4 → r9
48 83 EC 38           sub rsp, 0x38        ; allocate shadow space + frame
49 BA [8-byte gadget] mov r10, <gadget>    ; load gadget address
4C 89 14 24           mov [rsp], r10       ; [rsp] = gadget (fake return addr)
4C 89 5C 24 30        mov [rsp+0x30], r11  ; [rsp+0x30] = real return
FF E0                 jmp rax              ; jump to target
```

**Flow**: Target returns to gadget → `add rsp, 0x28; ret` → pops real return address → back to our code.

### 7.5 Typed Wrappers

```cpp
#define SpoofCall1(fn, a1)           ((fn_spoof_1)g_SpoofStub)((PVOID)(fn), (PVOID)(a1))
#define SpoofCall2(fn, a1, a2)       ((fn_spoof_2)g_SpoofStub)((PVOID)(fn), (PVOID)(a1), (PVOID)(a2))
#define SpoofCall3(fn, a1, a2, a3)   ...
#define SpoofCall4(fn, a1, a2, a3, a4) ...
```

**Usage pattern** (throughout the driver):
```cpp
if (g_SpoofStub)
    SpoofCall2(PsLookupProcessByProcessId, pid, &process);
else
    PsLookupProcessByProcessId(pid, &process);
```

### 7.6 Init

```cpp
InitSpoofCall():
  1. FindSpoofGadget() — scan ntoskrnl for 48 83 C4 28 C3
  2. InitSpoofStub() — allocate NonPagedPoolExecute, copy shellcode, patch gadget addr at offset 25
```

---

## 8. Trace Cleaning ([cleaner.h](

### 8.1 PiDDB Cache Table

The PiDDB (Plug and Play Database) caches information about every driver ever loaded. EAC checks this.

**Locate PiDDB**:
```
PiDDBLockPtr pattern:  48 8D 0D CC CC CC CC E8 CC CC CC CC 48 8B 0D CC CC CC CC 33 DB
PiDTablePtr pattern:   48 8D 0D CC CC CC CC E8 CC CC CC CC 3D CC CC CC CC 0F 83 CC CC CC CC
```

Both scanned in ntoskrnl's `PAGE` section. Results are RVAs — add `g_KernelBase` then resolve RIP-relative address.

**Clean**:
```cpp
lookupEntry.DriverName = L"iqvw64e.sys";
lookupEntry.TimeDateStamp = 0x5284EAC3;

ExAcquireResourceExclusiveLite(PiDDBLock, TRUE);
pFound = RtlLookupElementGenericTableAvl(PiDDBCacheTable, &lookupEntry);
if (pFound) {
    RemoveEntryList(&pFound->List);
    RtlDeleteElementGenericTableAvl(PiDDBCacheTable, pFound);
}
ExReleaseResourceLite(PiDDBLock);
```

### 8.2 MmUnloadedDrivers

```
Pattern: 4C 8B CC CC CC CC CC 4C 8B C9 4D 85 CC 74
```

Scanned in ntoskrnl `PAGE` section. Resolves to [MmUnloadedDrivers](

**Clean**: Iterate 50 entries, find `iqvw64e` or `Iqvw64e`, zero out `Name.Buffer` and the entire `MM_UNLOADED_DRIVER` struct.

### 8.3 Driver Object Hiding

```cpp
HideDriverObject(DriverObject):
  1. Clear DriverName buffer (RtlZeroMemory)
  2. Unlink from PsLoadedModuleList (prev->Flink = next, next->Blink = prev)
  3. Erase PE headers at DriverStart (RtlZeroMemory up to SizeOfHeaders)
  4. NULL out: DriverSection, DriverInit, DriverStart, DriverSize
  5. NULL all MajorFunction[0..IRP_MJ_MAXIMUM_FUNCTION] pointers
```

### 8.4 Master Clean Call

```cpp
CleanAllTraces(DriverObject):
  1. CleanPiDDBCacheTable()      — remove iqvw64e.sys load record
  2. CleanMmUnloadedDrivers()    — remove unload record (non-fatal)
  3. HideDriverObject()          — completely hide ourselves
```

---

## 9. Virtual Memory Operations ([memory.cpp](

### 9.1 System Module Helpers

```cpp
GetSystemModuleBase("dxgkrnl.sys"):
  → ZwQuerySystemInformation(SystemModuleInformation) to enumerate all loaded modules
  → Compare filename, return ImageBase

GetSystemModuleExport("dxgkrnl.sys", "NtQueryCompositionSurfaceStatistics"):
  → GetSystemModuleBase → RtlFindExportedRoutineByName
```

### 9.2 MmCopyVirtualMemory R/W (Fallback)

```cpp
myReadProcessMemory(pid, address, buffer, size):
  → PsLookupProcessByProcessId(pid, &process)
  → MmCopyVirtualMemory(process, address, PsGetCurrentProcess(), buffer, size, KernelMode, &bytes)
  → ObfDereferenceObject(process)
```

This is the **fallback** method — used only for `CMD_READ64`/`CMD_WRITE64`. The primary `CMD_READ`/`CMD_WRITE` uses physical memory R/W instead.

### 9.3 Module Base via PEB Walk

```cpp
GetProcessModuleBase(pid, moduleName):
  1. PsLookupProcessByProcessId → EPROCESS
  2. Copy moduleName to kernel stack BEFORE attaching (CRITICAL!)
  3. KeStackAttachProcess
  4. Walk PEB → Ldr → ModuleListLoadOrder
  5. Compare BaseDllName with moduleName
  6. Return DllBase
  7. KeUnstackDetachProcess
```

> **CRITICAL GOTCHA**: `moduleName` points to usermode memory of the CALLING process. After `KeStackAttachProcess`, address space switches to TARGET — that pointer becomes invalid! Always copy to kernel stack first.

### 9.4 Virtual Memory Management

All use `KeStackAttachProcess` → `Zw*` APIs → `KeUnstackDetachProcess`:
- **Alloc**: `ZwAllocateVirtualMemory(ZwCurrentProcess(), &base, 0, &regionSize, MEM_COMMIT|MEM_RESERVE, protect)`
- **Free**: `ZwFreeVirtualMemory(ZwCurrentProcess(), &base, &regionSize, MEM_RELEASE)`
- **Protect**: `ZwProtectVirtualMemory(ZwCurrentProcess(), &addr, &regionSize, protection, &oldProtect)`

---

## 10. Kernel Imports ([definitions.h](

### Undocumented APIs Used

```cpp
extern "C" {
    PPEB_KM PsGetProcessPeb(IN PEPROCESS Process);

    NTSTATUS MmCopyVirtualMemory(
        PEPROCESS SourceProcess, PVOID SourceAddress,
        PEPROCESS TargetProcess, PVOID TargetAddress,
        SIZE_T BufferSize, KPROCESSOR_MODE PreviousMode, PSIZE_T ReturnSize);

    NTSTATUS ZwProtectVirtualMemory(
        HANDLE ProcessHandle, PVOID* BaseAddress,
        PSIZE_T ProtectSize, ULONG NewProtect, PULONG OldProtect);

    PIMAGE_NT_HEADERS RtlImageNtHeader(PVOID Base);

    PVOID RtlFindExportedRoutineByName(PVOID ImageBase, PCCH RoutineName);

    NTSTATUS ZwQuerySystemInformation(
        ULONG InfoClass, PVOID Buffer, ULONG Length, PULONG ReturnLength);
}
```

### Custom Kernel Structures

```cpp
// PEB / LDR for module enumeration
typedef struct _PEB_KM {
    UCHAR Reserved1[2];
    UCHAR BeingDebugged;
    UCHAR Reserved2[1];
    PVOID Reserved3[2];
    PPEB_LDR_DATA_KM Ldr;
} PEB_KM;

typedef struct _LDR_DATA_TABLE_ENTRY_KM {
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
} LDR_DATA_TABLE_ENTRY_KM;

// PiDDB structures for cleaning
typedef struct PiDDBCacheEntry {
    LIST_ENTRY      List;
    UNICODE_STRING  DriverName;
    ULONG           TimeDateStamp;
    NTSTATUS        LoadStatus;
    char            _pad[16];
} PIDCacheobj;

// System module enumeration
typedef struct _RTL_PROCESS_MODULE_INFORMATION {
    ULONG Section; PVOID MappedBase; PVOID ImageBase;
    ULONG ImageSize; ULONG Flags; USHORT LoadOrderIndex;
    USHORT InitOrderIndex; USHORT LoadCount;
    USHORT OffsetToFileName; CHAR FullPathName[256];
} RTL_PROCESS_MODULE_INFORMATION;
```

---

## 11. Key Anti-Detection Summary

| Threat | Mitigation |
|--------|-----------|
| **Code integrity checks** | PTE hook — original physical page untouched |
| **Stack walk detection** | Return address spoofing via ntoskrnl gadget |
| **MmCopyVirtualMemory monitoring** | Physical memory R/W via CR3 page walk |
| **CR3 from EPROCESS encrypted** | `__readcr3()` via `KeStackAttachProcess` |
| **PiDDB cache** | Cleaned on init (iqvw64e.sys entry removed) |
| **MmUnloadedDrivers** | Cleaned on init |
| **Driver enumeration** | DriverObject NULLed, PsLoadedModuleList unlinked |
| **PE header analysis** | Headers zeroed with RtlZeroMemory |
| **User→kernel communication** | Hooked existing dxgkrnl function (no custom device/IOCTL) |

---

## 12. Build Notes

- **WDK required**: Windows Driver Kit (latest)
- **Pool tag convention**: `'mEmK'`, `'rPhM'`, `'wPhM'`, `'etPK'`, `'fpSK'`, `'pfSK'`, `'cLnK'`
- **No DriverUnload**: Driver cannot be conventionally unloaded (stays resident). PTE hook can be restored via `RestorePteHook()` if needed.
- **No IOCTL**: No `IoCreateDevice`, no `IRP_MJ_DEVICE_CONTROL`. All communication via hooked function.
- **iqvw64e.sys timestamp**: `0x5284EAC3` — hardcoded for PiDDB lookup.
- **Spoof stub memory**: Allocated from `NonPagedPoolExecute` — needed for shellcode execution.
