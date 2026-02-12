#pragma once
/*
 * driver_comm.h - Driver communication via hooked dxgkrnl function
 *
 * Uses NtQueryCompositionSurfaceStatistics (hooked in dxgkrnl.sys)
 * to send commands to the kernel driver.
 */

#include <windows.h>
#include <winternl.h>
#include <cstdio>
#include "../shared.h"

/* ── NtQueryCompositionSurfaceStatistics ─────────────────────────── */

typedef NTSTATUS(NTAPI* fn_NtQueryCompositionSurfaceStatistics)(PVOID);

class DriverComm {
private:
    fn_NtQueryCompositionSurfaceStatistics pNtQuery = nullptr;
    bool connected = false;

    bool SendRequest(REQUEST_DATA* req) {
        if (!pNtQuery) return false;
        req->magic = REQUEST_MAGIC;
        __try {
            pNtQuery(req);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        return true;
    }

public:
    bool Init() {
        HMODULE hWin32u = LoadLibraryA("win32u.dll");
        if (!hWin32u) return false;

        pNtQuery = (fn_NtQueryCompositionSurfaceStatistics)
            GetProcAddress(hWin32u, "NtQueryCompositionSurfaceStatistics");
        if (!pNtQuery) return false;

        /* Ping to verify hook is active */
        REQUEST_DATA req = { 0 };
        req.command = CMD_PING;
        req.magic = REQUEST_MAGIC;
        if (!SendRequest(&req)) return false;

        connected = (req.result == 0x50544548 || req.result == 0x4B524E4C);
        return connected;
    }

    bool IsConnected() const { return connected; }

    bool ReadMemory(DWORD pid, uintptr_t address, void* buffer, size_t size) {
        REQUEST_DATA req = { 0 };
        req.command = CMD_READ;
        req.pid = pid;
        req.address = address;
        req.buffer = (unsigned __int64)buffer;
        req.size = size;
        return SendRequest(&req) && req.result;
    }

    bool WriteMemory(DWORD pid, uintptr_t address, void* buffer, size_t size) {
        REQUEST_DATA req = { 0 };
        req.command = CMD_WRITE;
        req.pid = pid;
        req.address = address;
        req.buffer = (unsigned __int64)buffer;
        req.size = size;
        return SendRequest(&req) && req.result;
    }

    uintptr_t GetModuleBase(DWORD pid, const wchar_t* moduleName) {
        REQUEST_DATA req = { 0 };
        req.command = CMD_MODULE_BASE;
        req.pid = pid;
        wcsncpy_s(req.module_name, 64, moduleName, _TRUNCATE);
        SendRequest(&req);
        return (uintptr_t)req.result;
    }

    uintptr_t AllocMemory(DWORD pid, size_t size, DWORD protect) {
        REQUEST_DATA req = { 0 };
        req.command = CMD_ALLOC;
        req.pid = pid;
        req.size = size;
        req.protect = protect;
        SendRequest(&req);
        return (uintptr_t)req.result;
    }

    void FreeMemory(DWORD pid, uintptr_t address) {
        REQUEST_DATA req = { 0 };
        req.command = CMD_FREE;
        req.pid = pid;
        req.result = address;
        SendRequest(&req);
    }

    void ProtectMemory(DWORD pid, uintptr_t address, size_t size, DWORD protect) {
        REQUEST_DATA req = { 0 };
        req.command = CMD_PROTECT;
        req.pid = pid;
        req.address = address;
        req.size = size;
        req.protect = protect;
        SendRequest(&req);
    }

    /* ── Templated helpers ───────────────────────────────────────── */

    template<typename T>
    T Read(DWORD pid, uintptr_t address) {
        T value{};
        ReadMemory(pid, address, &value, sizeof(T));
        return value;
    }

    template<typename T>
    bool Write(DWORD pid, uintptr_t address, const T& value) {
        return WriteMemory(pid, address, (void*)&value, sizeof(T));
    }
};
