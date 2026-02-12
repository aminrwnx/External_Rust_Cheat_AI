# Driver Stealth Mechanisms & Undetectability

This document explains the specific techniques used in the kernel driver to evade detection by Easy Anti-Cheat (EAC) and other anti-cheat solutions.

## 1. Trace Cleaning (removing evidence of loading)

Since the driver is manually mapped (using kdmapper/iqvw64e.sys), it leaves traces in system structures that anti-cheats scan. We clean these immediately upon loading.

### A. PiDDB Cache Table ([cleaner.h](file:///C:/Users/MSZ/source/repos/Krnld/Krnld/driver/cleaner.h))
**What it is:** The Kernel Plug and Play Database (PiDDB) caches information about every driver ever loaded, including the vulnerable `iqvw64e.sys` used for mapping.
**The Fix:**
1.  Locate `PiDDBLock` and [PiDDBCacheTable](file:///C:/Users/MSZ/source/repos/Krnld/Krnld/driver/kdmapper-master/kdmapper/gdrv_driver.cpp#1092-1224) in `ntoskrnl.exe` via pattern scanning.
2.  Acquire the lock exclusively.
3.  Search for the entry corresponding to `iqvw64e.sys` (timestamp `0x5284EAC3`).
4.  Unlink and delete the entry from the table.
5.  Release the lock.

### B. MmUnloadedDrivers ([cleaner.h](file:///C:/Users/MSZ/source/repos/Krnld/Krnld/driver/cleaner.h))
**What it is:** Windows keeps a list of recently unloaded drivers. The vulnerable mapper driver appears here after it unloads.
**The Fix:**
1.  Locate [MmUnloadedDrivers](file:///C:/Users/MSZ/source/repos/Krnld/Krnld/driver/kdmapper-master/kdmapper/gdrv_driver.cpp#916-1005) array and count.
2.  Iterate through the list.
3.  Directly overwrite the `MM_UNLOADED_DRIVER` entry for `iqvw64e.sys` with zeros or random data.

### C. Driver Object Hiding ([cleaner.h](file:///C:/Users/MSZ/source/repos/Krnld/Krnld/driver/cleaner.h))
**What it is:** The `DRIVER_OBJECT` structure itself is a major indicator of a loaded driver.
**The Fix:**
1.  **Unlink from PsLoadedModuleList**: Removes the driver from the standard list of loaded kernel modules (so it doesn't show up in tools like Process Hacker).
2.  **Erase Headers**: Overwrite the PE headers (DOS/NT headers) at the driver's base address with zeros. This prevents memory scanners from identifying it as a valid PE image.
3.  **Nullify Fields**: Zero out critical fields in the `DRIVER_OBJECT` structure: `DriverSection`, `DriverInit`, `DriverStart`, `DriverSize`, `DriverUnload`, and all `MajorFunction` pointers.

---

## 2. Return Address Spoofing ([spoof_call.h](file:///C:/Users/MSZ/source/repos/Krnld/Krnld/driver/spoof_call.h))

### The Problem
When our driver calls a kernel API (like `PsLookupProcessByProcessId`), the return address pushed onto the stack points to our driver's memory.
Since our driver is manually mapped and "hidden" (unlinked), this memory is **not inside any valid module**.
EAC performs **stack walks** on API calls and flags any return address that points to "unknown" memory.

### The Solution (Gadget-based Spoofing)
We never call kernel APIs directly. Instead, we jump to a "gadget" inside `ntoskrnl.exe`.

1.  **The Gadget**: We find a sequence like `add rsp, 0x28; ret` inside `ntoskrnl`.
2.  **The Stub**: A small assembly shellcode (`g_SpoofStub`) prepares the stack:
    *   Preserves the *real* return address (back to our driver).
    *   Pushes the *gadget* address as the fake return address.
    *   Jumps to the target function (e.g., `PsLookupProcessByProcessId`).
3.  **The Bypass**:
    *   The target function runs.
    *   When it returns, it "returns" to the gadget in `ntoskrnl`.
    *   Anti-cheat sees a return address **inside ntoskrnl** (legitimate).
    *   The gadget executes (`add rsp, 0x28; ret`) and returns control back to our stub, which returns to our driver.

---

## 3. Communication Stealth ([hook.cpp](file:///C:/Users/MSZ/source/repos/Krnld/Krnld/driver/hook.cpp))

### The Hook
We do NOT use `IoCreateDevice` or standard IOCTLs (which are easily enumerated).
Instead, we hook an existing function in `dxgkrnl.sys`: `NtQueryCompositionSurfaceStatistics`.

### PTE Hooking (Page Table Entry)
We don't overwrite the code bytes (inline hook) because `PatchGuard` and EAC check for integrity violations.
Instead, we modify the **Page Table Entry (PTE)**:
1.  Allocate a **new physical page**.
2.  Copy the original page content to the new page.
3.  Write our hook (trampoline) into the **new page**.
4.  Modify the PTE to point to the **new physical address**.

**Why it's undetected:**
*   **Integrity Checks**: If EAC reads the *physical* memory at the original address, they see the original, unmodified code.
*   **Execution**: The CPU, using virtual addresses, executes from our new page (with the hook).

---

## 4. Physical Memory R/W ([physical_memory.h](file:///C:/Users/MSZ/source/repos/Krnld/Krnld/driver/physical_memory.h))

We do NOT use `MmCopyVirtualMemory` for reading, as this function is heavily monitored.

**Undetected Read:**
1.  Get the target process's directory base (CR3).
2.  Manually walk the 4-level page table (PML4 -> PDPT -> PD -> PT) to translate virtual addresses to physical addresses.
3.  Read the **physical memory** directly.
    *   This bypasses all virtual memory hooks and monitoring.

**Undetected Write:**
We use `KeStackAttachProcess` + `RtlCopyMemory`. While simpler, it's safer for writes because:
*   Writing to physical memory is dangerous (COW, paging issues).
*   EAC focuses primarily on *reads* (esp/overlays), so direct writes are less scrutinized than reads.
