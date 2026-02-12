#pragma once
/*
 * rust_sdk.h  -  Rust game memory reader for external ESP
 *
 * Uses DriverComm (kernel driver) to read game memory.
 * All entity iteration goes through BaseNetworkable entity list.
 */

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

#include "driver_comm.h"
#include "rust_offsets.h"

/* ── Math types ──────────────────────────────────────────────────── */

struct Vec3 {
    float x, y, z;
    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    float Length() const { return sqrtf(x * x + y * y + z * z); }
};

struct Vec2 {
    float x, y;
};

struct ViewMatrix {
    float m[4][4];
};

/* ── Player info (output of ReadPlayer) ──────────────────────────── */

struct PlayerData {
    uintptr_t   address;
    Vec3        position;
    Vec3        headPos;        /* position + eye offset */
    std::wstring name;
    uint64_t    teamID;
    uint32_t    flags;
    uint32_t    lifestate;
    bool        isVisible;
    bool        isSleeping;
    bool        isWounded;
    float       distance;       /* filled by caller */
};

/* ── Decryption helpers ──────────────────────────────────────────── */
/*
 * Rust (EAC-protected Unity/IL2CPP) encrypts some pointers.
 * The decryption routines are game-version-specific.
 * These implement the algorithms from the Feb 10 2026 patch.
 *
 * Since il2cpp_get_handle resolves GC handles, and we can't call
 * game code externally, we try the DIRECT pointer chain first.
 * If that fails (null/invalid), we fall back to manual decryption.
 */

namespace RustDecrypt {

    /* ── Patch 21886569 decrypt functions ──────────────────────── */

    /* Decrypt client_entities pointer
     * ROL 12, ADD 0x73400338, ROL 9 */
    static uintptr_t DecryptClientEntities(uintptr_t encrypted_qword)
    {
        uint32_t* parts = (uint32_t*)&encrypted_qword;
        for (int i = 0; i < 2; i++) {
            uint32_t v = parts[i];
            /* ROL 12 */
            v = (v << 12) | (v >> 20);
            v += 0x73400338u;
            /* ROL 9 */
            v = (v << 9) | (v >> 23);
            parts[i] = v;
        }
        return encrypted_qword;
    }

    /* Decrypt entity_list pointer
     * ROL 20, XOR 0xDA2510F8, ROL 4, XOR 0xFD0B1AB6 */
    static uintptr_t DecryptEntityList(uintptr_t encrypted_qword)
    {
        uint32_t* parts = (uint32_t*)&encrypted_qword;
        for (int i = 0; i < 2; i++) {
            uint32_t v = parts[i];
            /* ROL 20 */
            v = (v << 20) | (v >> 12);
            v ^= 0xDA2510F8u;
            /* ROL 4 */
            v = (v << 4) | (v >> 28);
            v ^= 0xFD0B1AB6u;
            parts[i] = v;
        }
        return encrypted_qword;
    }

    /* Decrypt player_eyes pointer
     * ADD 0x5A59459F, ROL 1, ADD 0x533DF48A, ROL 5 */
    static uintptr_t DecryptPlayerEyes(uintptr_t encrypted_qword)
    {
        uint32_t* parts = (uint32_t*)&encrypted_qword;
        for (int i = 0; i < 2; i++) {
            uint32_t v = parts[i];
            v += 0x5A59459Fu;
            /* ROL 1 (add eax, eax = shl 1) */
            v = (v << 1) | (v >> 31);
            v += 0x533DF48Au;
            /* ROL 5 */
            v = (v << 5) | (v >> 27);
            parts[i] = v;
        }
        return encrypted_qword;
    }

    /* Decrypt player_inventory pointer
     * SUB 0x600B999C, XOR 0xE017EC85, ROL 6 */
    static uintptr_t DecryptPlayerInventory(uintptr_t encrypted_qword)
    {
        uint32_t* parts = (uint32_t*)&encrypted_qword;
        for (int i = 0; i < 2; i++) {
            uint32_t v = parts[i];
            v -= 0x600B999Cu;
            v ^= 0xE017EC85u;
            /* ROL 6 */
            v = (v << 6) | (v >> 26);
            parts[i] = v;
        }
        return encrypted_qword;
    }

} // namespace RustDecrypt

/* ── Main SDK class ──────────────────────────────────────────────── */

class RustSDK {
private:
    DriverComm* drv;
    DWORD       pid            = 0;
    uintptr_t   gameAssembly   = 0;
    bool        attached       = false;

    /* Cached object addresses to avoid repeated chain walks */
    uintptr_t   entityBuffer   = 0;
    int         entityCount    = 0;

    /* GC Handle table for resolving encrypted/managed pointers */
    uintptr_t   gcHandleTable  = 0;

    /* Address of the bitmap global (for scanning nearby for objects array) */
    uintptr_t   bitmapGlobalAddr = 0;

    /* Debug: throttle spam — only print full chain every N frames */
    int         dbgFrame       = 0;
    bool        dbgVerbose()   { return (dbgFrame % 300) == 0; }

    /* Check if a value looks like a valid user-space pointer.
     * Floor is 68GB to exclude GC handle range (handles are < ~17GB).
     * All real game pointers are > 2TB (0x251C..., 0x7FFE..., etc). */
    static bool IsValidPtr(uintptr_t p) {
        return p > 0x10000000000ULL && p < 0x7FFFFFFFFFFF;
    }

    /* Check if a value looks like an 8-byte aligned managed object */
    static bool IsAlignedPtr(uintptr_t p) {
        return IsValidPtr(p) && (p & 7) == 0;
    }

    /* Check if a value looks like a GC handle.
     * IL2CPP GC handles: lower 32 bits only, type = handle & 7 (1-4), index = handle >> 3 */
    static bool IsGCHandle(uintptr_t val) {
        if (val == 0) return false;
        uint32_t handle = (uint32_t)(val & 0xFFFFFFFF);
        uint32_t type = handle & 7;
        uint32_t index = handle >> 3;
        /* Valid types: 1=Weak, 2=Normal/Pinned, 3=Pinned, 4=WeakTrack */
        return (type >= 1 && type <= 4 && index < 0x100000);
    }

    /* ---------- low-level helpers ---------- */

    template<typename T>
    T Read(uintptr_t addr) {
        return drv->Read<T>(pid, addr);
    }

    bool ReadRaw(uintptr_t addr, void* buf, size_t sz) {
        return drv->ReadMemory(pid, addr, buf, sz);
    }

    /* Read a C# System.String (UTF-16) -> wstring */
    std::wstring ReadString(uintptr_t strPtr, int maxChars = 64) {
        if (!strPtr) return L"";
        int len = Read<int>(strPtr + Offsets::Il2CppString::length);
        if (len <= 0 || len > maxChars) len = maxChars;
        std::wstring result(len, L'\0');
        ReadRaw(strPtr + Offsets::Il2CppString::chars,
                result.data(), len * sizeof(wchar_t));
        return result;
    }

    /* Follow a simple pointer chain (no decryption) */
    uintptr_t ReadChain(uintptr_t base, const std::vector<uint32_t>& offsets) {
        uintptr_t addr = base;
        for (auto off : offsets) {
            addr = Read<uintptr_t>(addr + off);
            if (!addr) return 0;
        }
        return addr;
    }

    /* ── GC Handle Table Discovery ──────────────────────────── */

    /*
     * Given function code bytes, find ALL RIP-relative references and
     * try each as the GC handle objects array.
     * Entries must be 8-byte aligned valid pointers or NULL.
     */
    bool TryExtractTableFromCode(uintptr_t funcAddr, const uint8_t* code, int codeLen) {
        for (int i = 0; i < codeLen - 7; i++) {
            uint8_t rex = code[i];
            if (rex != 0x48 && rex != 0x4C) continue;
            uint8_t op = code[i + 1];
            if (op != 0x8B && op != 0x8D) continue;
            uint8_t modrm = code[i + 2];
            if ((modrm & 0xC7) != 0x05) continue;

            int32_t disp = *(int32_t*)(code + i + 3);
            uintptr_t refAddr = funcAddr + i + 7 + disp;

            printf("[GC]   RIP-rel @ +%d -> global 0x%llX\n", i, (uint64_t)refAddr);

            uintptr_t val = Read<uintptr_t>(refAddr);
            printf("[GC]     val = 0x%llX\n", (uint64_t)val);

            /* Check if val points to bitmap (all FFs = allocation bitmask) */
            if (IsValidPtr(val)) {
                uintptr_t t0 = Read<uintptr_t>(val);
                uintptr_t t1 = Read<uintptr_t>(val + 8);
                printf("[GC]     [0]=0x%llX  [1]=0x%llX\n", (uint64_t)t0, (uint64_t)t1);

                /* Check for bitmap pattern (high bits set = allocation mask) */
                bool isBitmap = (t0 > 0x7FFFFFFFFFFF || t1 > 0x7FFFFFFFFFFF);
                if (isBitmap) {
                    printf("[GC]     Looks like bitmap, saving addr\n");
                    bitmapGlobalAddr = refAddr;
                    continue; /* keep searching */
                }

                /* Entries must be 8-byte aligned ptrs or NULL */
                bool t0ok = (t0 == 0 || IsAlignedPtr(t0));
                bool t1ok = (t1 == 0 || IsAlignedPtr(t1));
                if (t0ok && t1ok && (t0 != 0 || t1 != 0)) {
                    gcHandleTable = val;
                    printf("[GC] ==> Handle table: 0x%llX\n", (uint64_t)gcHandleTable);
                    return true;
                }
                printf("[GC]     Rejected (not aligned object ptrs)\n");
            }

            /* Struct dereference */
            if (val && !IsValidPtr(val)) continue;
            if (IsValidPtr(val)) {
                /* Try +0x00 and +0x08 as struct fields pointing to objects array */
                for (int off = 0; off <= 0x18; off += 8) {
                    uintptr_t inner = Read<uintptr_t>(val + off);
                    if (!IsValidPtr(inner)) continue;
                    uintptr_t e0 = Read<uintptr_t>(inner);
                    uintptr_t e1 = Read<uintptr_t>(inner + 8);
                    bool ok0 = (e0 == 0 || IsAlignedPtr(e0));
                    bool ok1 = (e1 == 0 || IsAlignedPtr(e1));
                    if (ok0 && ok1 && (e0 != 0 || e1 != 0)) {
                        gcHandleTable = inner;
                        printf("[GC] ==> Handle table (deref+%d): 0x%llX\n", off, (uint64_t)gcHandleTable);
                        return true;
                    }
                }
            }
        }
        return false;
    }

    /* ── Method A: Load from DISK + follow JMP thunks ── */

    bool FindGCTableFromDisk() {
        printf("[GC] Method A: Loading GameAssembly.dll from disk...\n");

        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!hProc) {
            printf("[GC]   OpenProcess failed (err=%lu)\n", GetLastError());
            return false;
        }
        wchar_t exePath[MAX_PATH] = {};
        DWORD pathLen = MAX_PATH;
        BOOL pathOk = QueryFullProcessImageNameW(hProc, 0, exePath, &pathLen);
        CloseHandle(hProc);
        if (!pathOk) return false;

        wchar_t* lastSlash = wcsrchr(exePath, L'\\');
        if (!lastSlash) return false;
        wcscpy_s(lastSlash + 1, MAX_PATH - (int)(lastSlash + 1 - exePath), L"GameAssembly.dll");

        wprintf(L"[GC]   Path: %s\n", exePath);

        HMODULE hMod = LoadLibraryExW(exePath, NULL,
            LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_AS_DATAFILE);
        if (!hMod) {
            printf("[GC]   LoadLibraryExW failed (err=%lu)\n", GetLastError());
            return false;
        }
        uintptr_t fileBase = (uintptr_t)hMod & ~(uintptr_t)3;

        IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)fileBase;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) { FreeLibrary(hMod); return false; }
        IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)(fileBase + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) { FreeLibrary(hMod); return false; }

        auto& expDD = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!expDD.VirtualAddress) { FreeLibrary(hMod); return false; }

        IMAGE_EXPORT_DIRECTORY* expDir = (IMAGE_EXPORT_DIRECTORY*)(fileBase + expDD.VirtualAddress);
        DWORD* names    = (DWORD*)(fileBase + expDir->AddressOfNames);
        WORD*  ordinals = (WORD*)(fileBase + expDir->AddressOfNameOrdinals);
        DWORD* funcs    = (DWORD*)(fileBase + expDir->AddressOfFunctions);

        printf("[GC]   %lu named exports\n", expDir->NumberOfNames);

        uint32_t targetRVA = 0;
        for (DWORD i = 0; i < expDir->NumberOfNames; i++) {
            const char* name = (const char*)(fileBase + names[i]);
            if (strcmp(name, "il2cpp_gchandle_get_target") == 0) {
                targetRVA = funcs[ordinals[i]];
                printf("[GC]   Found! RVA=0x%X\n", targetRVA);
                break;
            }
        }
        FreeLibrary(hMod);
        if (!targetRVA) return false;

        /* Read function code — follow JMP thunks */
        uintptr_t funcAddr = gameAssembly + targetRVA;
        uint8_t code[512] = {};
        ReadRaw(funcAddr, code, sizeof(code));

        /* If first instruction is JMP (E9), follow it */
        if (code[0] == 0xE9) {
            int32_t jmpDelta = *(int32_t*)(code + 1);
            uintptr_t realFunc = funcAddr + 5 + jmpDelta;
            printf("[GC]   JMP thunk -> real function at 0x%llX\n", (uint64_t)realFunc);
            funcAddr = realFunc;
            memset(code, 0, sizeof(code));
            ReadRaw(funcAddr, code, sizeof(code));
        }

        printf("[GC]   Code @ 0x%llX: ", (uint64_t)funcAddr);
        for (int k = 0; k < 48; k++) printf("%02X ", code[k]);
        printf("\n");

        if (TryExtractTableFromCode(funcAddr, code, 500))
            return true;

        printf("[GC]   Function parsed but table not found yet\n");
        return false;
    }

    /* ── Method B: Find through decrypt function CALL targets ── */

    bool FindGCTableFromDecryptFn() {
        printf("[GC] Method B: Parsing decrypt functions...\n");

        const uintptr_t decryptRVAs[] = {
            Offsets::Decrypt::entity_list_fn,
            Offsets::Decrypt::client_entities_fn,
            Offsets::Decrypt::player_eyes_fn,
        };

        for (auto rva : decryptRVAs) {
            uintptr_t fnAddr = gameAssembly + rva;
            uint8_t code[512] = {};
            if (!ReadRaw(fnAddr, code, sizeof(code))) continue;
            if (code[0] == 0x00 && code[1] == 0x00) continue;

            printf("[GC]   Decrypt fn @ RVA 0x%llX\n", (uint64_t)rva);

            for (int i = 0; i < 500; i++) {
                if (code[i] != 0xE8) continue;
                int32_t callDelta = *(int32_t*)(code + i + 1);
                uintptr_t callTarget = fnAddr + i + 5 + callDelta;
                if (callTarget < gameAssembly || callTarget > gameAssembly + 0x20000000)
                    continue;

                printf("[GC]   CALL @ +%d -> 0x%llX\n", i, (uint64_t)callTarget);

                /* Read target function, follow JMP if needed */
                uint8_t targetCode[512] = {};
                ReadRaw(callTarget, targetCode, sizeof(targetCode));

                if (targetCode[0] == 0xE9) {
                    int32_t jd = *(int32_t*)(targetCode + 1);
                    uintptr_t real = callTarget + 5 + jd;
                    printf("[GC]     -> follows JMP to 0x%llX\n", (uint64_t)real);
                    callTarget = real;
                    ReadRaw(callTarget, targetCode, sizeof(targetCode));
                }

                if (TryExtractTableFromCode(callTarget, targetCode, 500))
                    return true;
            }
        }

        printf("[GC]   Not found via decrypt functions\n");
        return false;
    }

    /* ── Method C: Scan globals near the bitmap for the objects array ── */

    bool FindGCTableNearBitmap() {
        if (!bitmapGlobalAddr) return false;

        printf("[GC] Method C: Scanning globals near bitmap (0x%llX)...\n",
               (uint64_t)bitmapGlobalAddr);

        /* Scan ±0x200 bytes around the bitmap global for pointer-to-objects-array */
        for (int delta = -0x200; delta <= 0x200; delta += 8) {
            if (delta == 0) continue; /* skip the bitmap global itself */
            uintptr_t scanAddr = bitmapGlobalAddr + delta;
            uintptr_t val = Read<uintptr_t>(scanAddr);
            if (!IsValidPtr(val)) continue;

            /* Check first few entries: should be aligned ptrs or NULL */
            uintptr_t e0 = Read<uintptr_t>(val);
            uintptr_t e1 = Read<uintptr_t>(val + 8);
            uintptr_t e2 = Read<uintptr_t>(val + 16);

            bool ok0 = (e0 == 0 || IsAlignedPtr(e0));
            bool ok1 = (e1 == 0 || IsAlignedPtr(e1));
            bool ok2 = (e2 == 0 || IsAlignedPtr(e2));

            /* At least 2 of 3 entries should be valid (or null), and not ALL null */
            int validCount = (e0 != 0 && IsAlignedPtr(e0)) +
                             (e1 != 0 && IsAlignedPtr(e1)) +
                             (e2 != 0 && IsAlignedPtr(e2));

            if (ok0 && ok1 && ok2 && validCount >= 1) {
                printf("[GC]   +0x%X: val=0x%llX  [0]=0x%llX [1]=0x%llX [2]=0x%llX\n",
                       delta, (uint64_t)val, (uint64_t)e0, (uint64_t)e1, (uint64_t)e2);

                gcHandleTable = val;
                printf("[GC] ==> Handle table (near bitmap): 0x%llX\n", (uint64_t)gcHandleTable);
                return true;
            }
        }

        printf("[GC]   No objects array found near bitmap\n");
        return false;
    }

    /* ── Master: try all methods ── */

    bool FindGCHandleTable() {
        if (FindGCTableFromDisk()) return true;
        if (FindGCTableFromDecryptFn()) return true;
        /* Even if we didn't find a flat table, bitmapGlobalAddr may have been
         * set — that's the base of the per-type handle array which is all we need */
        if (bitmapGlobalAddr) {
            printf("[GC] Using bitmap base as type array: 0x%llX\n", (uint64_t)bitmapGlobalAddr);
            return true;
        }
        return false;
    }

    /*
     * Resolve a GC handle based on actual il2cpp_gchandle_get_target disassembly:
     *
     *   mov ebx, ecx          ; handle is 32-bit
     *   lea rax, [rip+BASE]   ; rax = base of type array in .bss
     *   and ecx, 7            ; type = handle & 7
     *   shr ebx, 3            ; index = handle >> 3
     *   dec ecx               ; type_idx = type - 1
     *   lea rdx, [rcx+rcx*4]  ; rdx = type_idx * 5
     *   lea rdi, [rax+rdx*8]  ; rdi = base + type_idx * 40
     *
     * So each handle type has a 40-byte (5 qword) record.
     * Record layout (likely):
     *   +0x00: bitmap pointer (allocation bitmap)
     *   +0x08: objects array pointer (Il2CppObject**)
     *   +0x10: capacity / metadata
     *   +0x18: metadata
     *   +0x20: metadata
     */
    uintptr_t ResolveGCHandle(uintptr_t rawHandle) {
        if (!bitmapGlobalAddr || !rawHandle) return 0;

        uint32_t handle = (uint32_t)(rawHandle & 0xFFFFFFFF);
        if (handle == 0) return 0;

        uint32_t type = handle & 7;
        uint32_t index = handle >> 3;

        if (type == 0 || type > 4) return 0; /* types are 1-4 typically */

        /* Record address = base + (type-1) * 40 */
        uintptr_t recordAddr = bitmapGlobalAddr + (uintptr_t)(type - 1) * 40;

        /* Read all 5 qwords of the record */
        uintptr_t record[5] = {};
        ReadRaw(recordAddr, record, sizeof(record));

        static int gcDbgCount = 0;
        bool verbose = (gcDbgCount++ < 10);

        if (verbose) {
            printf("[GCR] handle=0x%X type=%u idx=%u record@0x%llX\n",
                   handle, type, index, (uint64_t)recordAddr);
            for (int i = 0; i < 5; i++)
                printf("[GCR]   [%d] = 0x%llX\n", i, (uint64_t)record[i]);
        }

        /* Try fields in order: [1] objects array first, then [4], [0], [2], [3]
         * Field[0] is typically the bitmap — it can contain values that
         * look like valid pointers but are actually bitmap entries. */
        static const int fieldOrder[] = {1, 4, 0, 2, 3};
        for (int fi = 0; fi < 5; fi++) {
            int field = fieldOrder[fi];
            uintptr_t objArrayPtr = record[field];
            if (!IsValidPtr(objArrayPtr)) continue;

            uintptr_t target = Read<uintptr_t>(objArrayPtr + (uintptr_t)index * 8);
            if (IsValidPtr(target)) {
                if (verbose) {
                    printf("[GCR]   field[%d]=0x%llX -> [%u]=0x%llX VALID!\n",
                           field, (uint64_t)objArrayPtr, index, (uint64_t)target);
                }
                return target;
            }
        }

        if (verbose) printf("[GCR]   No valid resolution found\n");
        return 0;
    }

    /* ── Decrypt constant extraction from game code ────────── */
    /*
     * Scan a decrypt function for XOR/ROL/ROR/ADD/SUB with immediates.
     * This lets us dynamically discover the correct constants for any build.
     */

    void DumpDecryptFunction(const char* name, uintptr_t rva) {
        uintptr_t fnAddr = gameAssembly + rva;
        uint8_t code[512] = {};
        if (!ReadRaw(fnAddr, code, sizeof(code))) return;

        printf("\n[DECRYPT] === %s @ RVA 0x%llX (VA 0x%llX) ===\n",
               name, (uint64_t)rva, (uint64_t)fnAddr);

        /* Print full hex dump (first 256 bytes) */
        for (int row = 0; row < 256; row += 16) {
            printf("[DECRYPT] +%03X: ", row);
            for (int col = 0; col < 16; col++)
                printf("%02X ", code[row + col]);
            printf("\n");
        }

        /* Scan for arithmetic instructions with 32-bit immediates */
        printf("[DECRYPT] Detected operations:\n");

        for (int i = 0; i < 250; i++) {
            /* Check for REX prefix (41 = R8-R15) */
            bool hasREX = (code[i] == 0x41);
            int base = hasREX ? i + 1 : i;

            /* XOR eax, imm32 (opcode 35) */
            if (!hasREX && code[base] == 0x35 && base + 5 <= 256) {
                uint32_t imm = *(uint32_t*)(code + base + 1);
                printf("[DECRYPT]   +%03X: XOR eax, 0x%08X\n", i, imm);
            }
            /* SUB eax, imm32 (opcode 2D) */
            if (!hasREX && code[base] == 0x2D && base + 5 <= 256) {
                uint32_t imm = *(uint32_t*)(code + base + 1);
                printf("[DECRYPT]   +%03X: SUB eax, 0x%08X\n", i, imm);
            }
            /* ADD eax, imm32 (opcode 05) */
            if (!hasREX && code[base] == 0x05 && base + 5 <= 256) {
                uint32_t imm = *(uint32_t*)(code + base + 1);
                printf("[DECRYPT]   +%03X: ADD eax, 0x%08X\n", i, imm);
            }

            /* Group 1: 81 /r rm32, imm32 */
            if (code[base] == 0x81 && base + 6 <= 256) {
                static const char* regs[]  = {"eax","ecx","edx","ebx","esp","ebp","esi","edi"};
                static const char* rregs[] = {"r8d","r9d","r10d","r11d","r12d","r13d","r14d","r15d"};
                static const char* ops[]   = {"ADD","OR","ADC","SBB","AND","SUB","XOR","CMP"};
                uint8_t modrm = code[base + 1];
                uint8_t mod = (modrm >> 6) & 3;
                uint8_t reg = (modrm >> 3) & 7;
                uint8_t rm  = modrm & 7;
                if (mod == 3) {
                    uint32_t imm = *(uint32_t*)(code + base + 2);
                    const char* rn = hasREX ? rregs[rm] : regs[rm];
                    printf("[DECRYPT]   +%03X: %s %s, 0x%08X\n", i, ops[reg], rn, imm);
                }
            }

            /* Group 2: C1 /r rm32, imm8 (ROL/ROR/SHL/SHR/SAR) */
            if (code[base] == 0xC1 && base + 3 <= 256) {
                static const char* regs[]  = {"eax","ecx","edx","ebx","esp","ebp","esi","edi"};
                static const char* rregs[] = {"r8d","r9d","r10d","r11d","r12d","r13d","r14d","r15d"};
                static const char* sops[]  = {"ROL","ROR","RCL","RCR","SHL","SHR","SAL","SAR"};
                uint8_t modrm = code[base + 1];
                uint8_t mod = (modrm >> 6) & 3;
                uint8_t reg = (modrm >> 3) & 7;
                uint8_t rm  = modrm & 7;
                if (mod == 3) {
                    uint8_t imm = code[base + 2];
                    const char* rn = hasREX ? rregs[rm] : regs[rm];
                    printf("[DECRYPT]   +%03X: %s %s, %d\n", i, sops[reg], rn, imm);
                }
            }
        }
        printf("[DECRYPT] === END ===\n\n");
    }

    void AnalyzeDecryptFunctions() {
        printf("\n[*] Analyzing decrypt functions for constants...\n");
        DumpDecryptFunction("entity_list_fn", Offsets::Decrypt::entity_list_fn);
        DumpDecryptFunction("client_entities_fn", Offsets::Decrypt::client_entities_fn);
    }

public:
    RustSDK(DriverComm* driver) : drv(driver) {}

    bool IsAttached() const { return attached && pid != 0; }
    DWORD GetPID()    const { return pid; }

    /* ── Attach to Rust process ──────────────────────────────── */

    bool Attach() {
        attached = false;
        entityBuffer = 0;
        entityCount  = 0;
        dbgFrame     = 0;

        printf("[*] Searching for RustClient.exe...\n");

        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return false;

        PROCESSENTRY32W pe = { sizeof(pe) };
        pid = 0;
        if (Process32FirstW(snap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"RustClient.exe") == 0) {
                    pid = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        if (!pid) {
            printf("[!] RustClient.exe not found\n");
            return false;
        }
        printf("[+] RustClient.exe PID: %lu\n", pid);

        printf("[*] Getting GameAssembly.dll base...\n");
        gameAssembly = drv->GetModuleBase(pid, L"GameAssembly.dll");
        if (!gameAssembly) {
            printf("[!] GameAssembly.dll not found\n");
            return false;
        }
        printf("[+] GameAssembly.dll base: 0x%llX\n", (uint64_t)gameAssembly);

        attached = true;
        printf("[+] Attached successfully!\n");

        printf("[*] Looking for GC handle table...\n");
        if (FindGCHandleTable()) {
            printf("[+] GC handle table found at 0x%llX\n", (uint64_t)gcHandleTable);
        } else {
            printf("[!] GC handle table not found\n");
        }

        /* Dump decrypt function code to extract correct constants */
        AnalyzeDecryptFunctions();

        return true;
    }

    /* ── Read camera view matrix ─────────────────────────────── */

    uintptr_t cachedCamBuf = 0; /* cached camera buffer to avoid re-traversing */

    bool GetViewMatrix(ViewMatrix& vm) {
        uintptr_t typeInfo = Read<uintptr_t>(
            gameAssembly + Offsets::MainCamera_TypeInfo);
        if (!typeInfo) return false;

        uintptr_t staticFields = Read<uintptr_t>(
            typeInfo + Offsets::CameraChain::static_fields);
        if (!staticFields) return false;

        uintptr_t instance = Read<uintptr_t>(
            staticFields + Offsets::CameraChain::instance);
        if (!instance) return false;

        uintptr_t buf = Read<uintptr_t>(
            instance + Offsets::CameraChain::buffer);
        if (!buf) return false;

        cachedCamBuf = buf; /* cache for GetCameraPosition */

        bool ok = ReadRaw(buf + Offsets::CameraChain::view_matrix,
                       &vm, sizeof(ViewMatrix));

        if (dbgVerbose()) {
            printf("[CAM] Chain: TypeInfo=0x%llX -> static=0x%llX -> inst=0x%llX -> buf=0x%llX\n",
                   (uint64_t)typeInfo, (uint64_t)staticFields,
                   (uint64_t)instance, (uint64_t)buf);
            printf("[CAM] ViewMatrix[0]: %.3f %.3f %.3f %.3f\n",
                   vm.m[0][0], vm.m[0][1], vm.m[0][2], vm.m[0][3]);
        }
        return ok;
    }

    Vec3 GetCameraPosition() {
        if (!cachedCamBuf) return {};
        return Read<Vec3>(cachedCamBuf + Offsets::CameraChain::position);
    }

    /* ── Decrypt-and-resolve helper ────────────────────────────
     *
     * All Rust encrypted fields follow the same pattern:
     *   1. The field stores a WRAPPER OBJECT pointer
     *   2. wrapper+0x14: flag byte (non-zero = encrypted)
     *   3. wrapper+0x18: 8 bytes of encrypted data
     *   4. Decrypt the 2 dwords using field-specific constants
     *   5. The decrypted value is a GC handle
     *   6. Resolve GC handle → actual object pointer
     */
    typedef uintptr_t(*DecryptFn)(uintptr_t);

    uintptr_t DecryptAndResolve(uintptr_t wrapperAddr, DecryptFn decrypt) {
        if (!wrapperAddr || !IsValidPtr(wrapperAddr)) return 0;

        /* Read the encrypted 8 bytes from wrapper+0x18 */
        uintptr_t encrypted = Read<uintptr_t>(wrapperAddr + 0x18);
        if (!encrypted) return 0;

        /* Apply the decrypt function */
        uintptr_t decrypted = decrypt(encrypted);

        /* The result is a GC handle — resolve it */
        uint32_t handle = (uint32_t)(decrypted & 0xFFFFFFFF);
        if (handle == 0) return 0;

        uintptr_t resolved = ResolveGCHandle(handle);

        static int dcrDbg = 0;
        if (dcrDbg++ < 20) {
            printf("[DCR] wrapper=0x%llX enc=0x%llX dec=0x%llX handle=0x%X -> 0x%llX %s\n",
                   (uint64_t)wrapperAddr, (uint64_t)encrypted, (uint64_t)decrypted,
                   handle, (uint64_t)resolved,
                   IsValidPtr(resolved) ? "VALID" : "INVALID");
        }

        return resolved;
    }

    /* ── Refresh entity list cache ─────────────────────────────
     *
     * Confirmed chain (Feb 2026):
     *   GameAssembly + 0xD655780 -> TypeInfo
     *   TypeInfo + 0xB8 -> staticFields
     *   staticFields + 0x30 -> wrapper1 (client_entities wrapper)
     *   decrypt_client_entities(wrapper1) -> clientEntities object
     *   clientEntities + 0x10 -> wrapper2 (entity_list wrapper)
     *   decrypt_entity_list(wrapper2) -> entityList object
     *   entityList + 0x10 -> buffer (Il2CppArray of entity pointers)
     */

    bool RefreshEntityList() {
        entityBuffer = 0;
        entityCount  = 0;
        dbgFrame++;

        uintptr_t typeInfo = Read<uintptr_t>(
            gameAssembly + Offsets::BaseNetworkable_TypeInfo);
        if (!typeInfo) return false;

        uintptr_t staticFields = Read<uintptr_t>(
            typeInfo + Offsets::EntityChain::static_fields);
        if (!staticFields) return false;

        /* ── Step 1: Read encrypted wrapper from staticFields+0x08 ── */
        uintptr_t wrapper1 = Read<uintptr_t>(
            staticFields + Offsets::EntityChain::client_entities);
        if (!IsValidPtr(wrapper1)) {
            if (dbgVerbose()) printf("[ENT] wrapper1 @ static+0x08 = 0x%llX INVALID\n",
                                     (uint64_t)wrapper1);
            return false;
        }

        /* ── Step 2: Decrypt client_entities ── */
        uintptr_t clientEntities = DecryptAndResolve(
            wrapper1, RustDecrypt::DecryptClientEntities);
        if (!IsValidPtr(clientEntities)) {
            if (dbgVerbose()) printf("[ENT] client_entities decrypt failed\n");
            return false;
        }

        /* ── Step 3: Read wrapper2 from CE+0x10, decrypt entity_list ── */
        uintptr_t wrapper2 = Read<uintptr_t>(clientEntities + 0x10);
        if (!IsValidPtr(wrapper2)) {
            if (dbgVerbose()) printf("[ENT] wrapper2 @ ce+0x10 = 0x%llX INVALID\n",
                                     (uint64_t)wrapper2);
            return false;
        }

        uintptr_t entityList = DecryptAndResolve(
            wrapper2, RustDecrypt::DecryptEntityList);
        if (!IsValidPtr(entityList)) {
            if (dbgVerbose()) printf("[ENT] entity_list decrypt failed\n");
            return false;
        }

        /* ── Step 4: Read objectDictionary from EL+0x20 ── */
        uintptr_t objDict = Read<uintptr_t>(
            entityList + Offsets::EntityChain::object_dictionary);
        if (!IsValidPtr(objDict)) {
            if (dbgVerbose()) printf("[ENT] objDict @ el+0x20 = 0x%llX INVALID\n",
                                     (uint64_t)objDict);
            return false;
        }

        /* ── Step 5: Read content + size from objDict ── */
        uintptr_t content = Read<uintptr_t>(
            objDict + Offsets::EntityChain::content);
        if (!IsValidPtr(content)) {
            if (dbgVerbose()) printf("[ENT] content @ objDict+0x10 = 0x%llX INVALID\n",
                                     (uint64_t)content);
            return false;
        }

        int count = Read<int>(objDict + Offsets::EntityChain::size);
        if (count <= 0 || count > 50000) {
            if (dbgVerbose()) printf("[ENT] size=%d out of range\n", count);
            return false;
        }

        entityBuffer = content;
        entityCount  = count;

        if (dbgVerbose()) {
            printf("[ENT] SUCCESS! Full chain: static+0x08 -> decrypt_ce -> ce+0x10 -> decrypt_el -> el+0x20 -> objDict\n");
            printf("[ENT]   wrapper1=0x%llX CE=0x%llX\n",
                   (uint64_t)wrapper1, (uint64_t)clientEntities);
            printf("[ENT]   wrapper2=0x%llX EL=0x%llX\n",
                   (uint64_t)wrapper2, (uint64_t)entityList);
            printf("[ENT]   objDict=0x%llX content=0x%llX count=%d\n",
                   (uint64_t)objDict, (uint64_t)content, count);

            /* Dump first 5 entities with class names */
            for (int i = 0; i < count && i < 5; i++) {
                uintptr_t ent = Read<uintptr_t>(
                    content + Offsets::Il2CppArray::first + i * 8);
                if (!ent) { printf("[ENT]   [%d] NULL\n", i); continue; }
                std::string cn = ReadClassName(ent);
                printf("[ENT]   [%d] 0x%llX %s class='%s'\n",
                       i, (uint64_t)ent, IsValidPtr(ent) ? "PTR" : "", cn.c_str());
            }
        }

        return true;
    }

    int GetEntityCount() const { return entityCount; }
    uintptr_t GetEntityBufferAddr() const { return entityBuffer; }

    /* Debug helpers for external access */
    bool ReadRawPublic(uintptr_t addr, void* buf, size_t sz) {
        return ReadRaw(addr, buf, sz);
    }
    template<typename T>
    T ReadVal(uintptr_t addr) { return Read<T>(addr); }

    /* ── Read a single entity address from the buffer ─────────── */

    uintptr_t GetEntity(int index) {
        if (!entityBuffer || index < 0 || index >= entityCount) return 0;
        return Read<uintptr_t>(
            entityBuffer + Offsets::Il2CppArray::first + index * 8);
    }

    /* ── Read IL2CPP class name from an entity ─────────────────
     *
     * IL2CPP object layout:
     *   entity + 0x00 = Il2CppClass* klass
     *   klass  + 0x10 = const char* name
     *   klass  + 0x18 = const char* namespaze
     *   klass  + 0x30 = Il2CppClass* parent
     */
    std::string ReadClassName(uintptr_t entity) {
        if (!entity || !IsValidPtr(entity)) return "";
        uintptr_t klass = Read<uintptr_t>(entity);
        if (!IsValidPtr(klass)) return "";
        uintptr_t namePtr = Read<uintptr_t>(klass + 0x10);
        if (!IsValidPtr(namePtr)) return "";
        char buf[64] = {};
        ReadRaw(namePtr, buf, 63);
        buf[63] = 0;
        return std::string(buf);
    }

    /* ── Check if entity is a BasePlayer ─────────────────────── */

    bool IsPlayer(uintptr_t entity) {
        if (!entity || !IsValidPtr(entity)) return false;
        /* Fast path: check class name directly (1 chain of reads) */
        std::string name = ReadClassName(entity);
        if (name == "BasePlayer" || name == "NPCPlayer" ||
            name == "ScientistNPC" || name == "HTNPlayer" ||
            name == "NPCMurderer" || name == "HumanNPC" ||
            name == "GingerbreadNPC") {
            return true;
        }
        return false;
    }

    /* ── Read player data ────────────────────────────────────── */

    bool ReadPlayer(uintptr_t entity, PlayerData& out) {
        out.address = entity;
        static int dbgPlayerPrintCount = 0;

        uintptr_t playerModel = Read<uintptr_t>(
            entity + Offsets::BasePlayer::playerModel);
        if (!playerModel) return false;

        out.position = Read<Vec3>(
            playerModel + Offsets::PlayerModel::position);

        if (out.position.x == 0.f && out.position.y == 0.f && out.position.z == 0.f)
            return false;

        out.headPos = Vec3(out.position.x, out.position.y + 1.6f, out.position.z);

        uintptr_t namePtr = Read<uintptr_t>(
            entity + Offsets::BasePlayer::displayName);
        out.name = ReadString(namePtr);

        out.teamID = Read<uint64_t>(
            entity + Offsets::BasePlayer::currentTeam);

        out.flags = Read<uint32_t>(
            entity + Offsets::BasePlayer::playerFlags);
        out.isSleeping = (out.flags & Offsets::PlayerFlags::IsSleeping) != 0;
        out.isWounded  = (out.flags & Offsets::PlayerFlags::Wounded)    != 0;

        out.lifestate = Read<uint32_t>(
            entity + Offsets::BaseCombatEntity::lifestate);

        out.isVisible = Read<bool>(
            entity + Offsets::BaseEntity::isVisible);

        if (dbgVerbose() && dbgPlayerPrintCount < 5) {
            char nameBuf[128] = {};
            WideCharToMultiByte(CP_UTF8, 0, out.name.c_str(), -1,
                                nameBuf, sizeof(nameBuf), nullptr, nullptr);
            printf("[PLR] 0x%llX | pos(%.1f, %.1f, %.1f) | name='%s' | flags=0x%X\n",
                   (uint64_t)entity, out.position.x, out.position.y, out.position.z,
                   nameBuf, out.flags);
            dbgPlayerPrintCount++;
        }
        if (dbgVerbose() && dbgFrame % 300 == 1) {
            dbgPlayerPrintCount = 0;
        }

        return true;
    }

    /* ── World to Screen ─────────────────────────────────────── */

    static bool WorldToScreen(const Vec3& world, const ViewMatrix& vm,
                              int screenW, int screenH, Vec2& out)
    {
        float w = vm.m[0][3] * world.x +
                  vm.m[1][3] * world.y +
                  vm.m[2][3] * world.z +
                  vm.m[3][3];
        if (w < 0.001f) return false;

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
};
