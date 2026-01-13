module;

#include <stdafx.h>
#include "MinHook.h"

export module Speedhack;

// =======================================================
// Globals
// =======================================================
//export float fFpsLimit;
static std::atomic<float> speedMultiplier{ 1.0f };
static float lastMultiplier = 1.0f;

static bool versionDetected = false;
export bool fAlternateSpinlock = false;
// =======================================================
// US / RU pointers
// =======================================================

export uint32_t* bPause = nullptr;
export uint32_t* bCutscene = nullptr;
export uint32_t* bLoading = nullptr;

// =======================================================
// CE-style spinlock
// =======================================================

struct SimpleLock {
    LONG count = 0;         // for CE spinlock
    DWORD owner = 0;        // thread ID
    LONG recursion = 0;     // for recursive mode

    void lock() {
        DWORD tid = GetCurrentThreadId();

        if (!fAlternateSpinlock) {
            // CE spinlock
            if (owner != tid) {
                while (InterlockedExchange(&count, 1) != 0)
                    Sleep(0);
                owner = tid;
            }
            else {
                InterlockedIncrement(&count);
            }
        }
        else {
            // Recursive spinlock
            if (owner == tid) {
                ++recursion;
                return;
            }
            while (InterlockedCompareExchange(&count, 1, 0) != 0)
                Sleep(0);
            owner = tid;
            recursion = 1;
        }
    }

    void unlock() {
        if (!fAlternateSpinlock) {
            // Classic CE spinlock
            if (count == 1)
                owner = 0;
            InterlockedDecrement(&count);
        }
        else {
            // Recursive spinlock
            if (--recursion == 0) {
                owner = 0;
                InterlockedExchange(&count, 0);
            }
        }
    }
};


static SimpleLock GTCLock;
static SimpleLock QPCLock;
static SimpleLock SYSLock;  // new lock for NtQuerySystemTime

// =======================================================
// Anchors
// =======================================================

static DWORD      initialTime32 = 0;
static DWORD      initialOffset32 = 0;

static ULONGLONG  initialTime64 = 0;
static ULONGLONG  initialOffset64 = 0;

static LONGLONG   initialTimeQPC = 0;
static LONGLONG   initialOffsetQPC = 0;

static LONGLONG   initialTimeSys = 0;    // new anchor for NtQuerySystemTime
static LONGLONG   initialOffsetSys = 0;  // new offset for NtQuerySystemTime

// =======================================================
// Original functions
// =======================================================

using FnGetTickCount = DWORD(WINAPI*)();
using FnGetTickCount64 = ULONGLONG(WINAPI*)();
using FnTimeGetTime = DWORD(WINAPI*)();
using FnQPC = BOOL(WINAPI*)(LARGE_INTEGER*);

using FnNtQuerySystemTime = NTSTATUS(NTAPI*)(PLARGE_INTEGER);
using FnNtQueryPerformanceCounter = NTSTATUS(NTAPI*)(PLARGE_INTEGER, PLARGE_INTEGER);

static FnGetTickCount realGetTickCount;
static FnGetTickCount64 realGetTickCount64;
static FnTimeGetTime realTimeGetTime;
static FnQPC realQPC;

static FnNtQuerySystemTime realNtQuerySystemTime;
static FnNtQueryPerformanceCounter realNtQueryPerformanceCounter;

// =======================================================
// Re-anchor (CRITICAL)
// =======================================================
export float fFpsLimit = 60.0f;   // default, safe value

static float ComputeGameSpeed()
{
    return (fFpsLimit > 0.0f) ? (30.0f / fFpsLimit) : 1.0f;
}

static float ComputeCutsceneSpeed()
{
    return (fFpsLimit > 0.0f) ? (60.0f / fFpsLimit) : 1.0f;
}

static void Reanchor(float newMultiplier)
{
    GTCLock.lock();
    QPCLock.lock();
    SYSLock.lock();

    float old = speedMultiplier.load();

    DWORD now32 = realGetTickCount();
    ULONGLONG now64 = realGetTickCount64();

    LARGE_INTEGER qpcNow{};
    realQPC(&qpcNow);

    LARGE_INTEGER sysNow{};
    realNtQuerySystemTime(&sysNow);

    initialOffset32 += DWORD((now32 - initialTime32) * old);
    initialOffset64 += ULONGLONG((now64 - initialTime64) * old);
    initialOffsetQPC += LONGLONG((qpcNow.QuadPart - initialTimeQPC) * old);
    initialOffsetSys += LONGLONG((sysNow.QuadPart - initialTimeSys) * old);

    initialTime32 = now32;
    initialTime64 = now64;
    initialTimeQPC = qpcNow.QuadPart;
    initialTimeSys = sysNow.QuadPart;

    speedMultiplier.store(newMultiplier);

    SYSLock.unlock();
    QPCLock.unlock();
    GTCLock.unlock();
}
// =======================================================
// Hooks
// =======================================================

DWORD WINAPI GetTickCount_Hook()
{
    GTCLock.lock();
    DWORD t = realGetTickCount();
    DWORD r = initialOffset32 + DWORD((t - initialTime32) * speedMultiplier.load());
    GTCLock.unlock();
    return r;
}

ULONGLONG WINAPI GetTickCount64_Hook()
{
    GTCLock.lock();
    ULONGLONG t = realGetTickCount64();
    ULONGLONG r = initialOffset64 + ULONGLONG((t - initialTime64) * speedMultiplier.load());
    GTCLock.unlock();
    return r;
}

DWORD WINAPI timeGetTime_Hook()
{
    return GetTickCount_Hook();
}

BOOL WINAPI QPC_Hook(LARGE_INTEGER* out)
{
    QPCLock.lock();
    LARGE_INTEGER t{};
    realQPC(&t);
    out->QuadPart = initialOffsetQPC + LONGLONG((t.QuadPart - initialTimeQPC) * speedMultiplier.load());
    QPCLock.unlock();
    return TRUE;
}
// ---------------- NT ----------------

NTSTATUS NTAPI NtQuerySystemTime_Hook(PLARGE_INTEGER out)
{
    NTSTATUS s = realNtQuerySystemTime(out);
    if (NT_SUCCESS(s)) {
        SYSLock.lock();
        out->QuadPart = initialOffsetSys + LONGLONG((out->QuadPart - initialTimeSys) * speedMultiplier.load());
        SYSLock.unlock();
    }
    return s;
}

NTSTATUS NTAPI NtQueryPerformanceCounter_Hook(PLARGE_INTEGER out, PLARGE_INTEGER freq)
{
    NTSTATUS s = realNtQueryPerformanceCounter(out, freq);
    if (NT_SUCCESS(s)) {
        QPCLock.lock();
        out->QuadPart = initialOffsetQPC + LONGLONG((out->QuadPart - initialTimeQPC) * speedMultiplier.load());
        QPCLock.unlock();
    }
    return s;
}


// =======================================================
// Watcher thread
// =======================================================

static void Watcher()
{
    while (true) {
        int cut = bCutscene ? *bCutscene : 0;
        int load = bLoading ? *bLoading : 0;

        float wanted;

        if (load) {
            wanted = 1.0f;
        }
        if (cut) {
            wanted = ComputeCutsceneSpeed();
        }
        if ((!cut) && (!load))
        {
            wanted = ComputeGameSpeed();
        }

        if (wanted != lastMultiplier) {
            Reanchor(wanted);
            lastMultiplier = wanted;
        }

        Sleep(50);
    }
}

// =======================================================
// Init
// =======================================================

export void InitSpeedhack()
{
    // ---------- version detection ----------
    auto pattern = hook::pattern("88 15 ? ? ? ? 8D 45");
    bPause = *pattern.get_first<uint32_t*>(2);

    pattern = hook::pattern("32 C0 88 81 ? ? ? ? A2 ? ? ? ? E8 ? ? ? ? 33 C0 C3");
    bCutscene = *pattern.get_first<uint32_t*>(9);

    pattern = hook::pattern("83 3D ? ? ? ? ? 74 ? 84 DB");
    bLoading = *pattern.get_first<uint32_t*>(2);

    // ---------- resolve ----------
    realGetTickCount = GetTickCount;
    realGetTickCount64 = GetTickCount64;
    realTimeGetTime = (FnTimeGetTime)GetProcAddress(GetModuleHandleW(L"winmm"), "timeGetTime");
    realQPC = QueryPerformanceCounter;

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    realNtQuerySystemTime = (FnNtQuerySystemTime)GetProcAddress(ntdll, "NtQuerySystemTime");
    realNtQueryPerformanceCounter = (FnNtQueryPerformanceCounter)GetProcAddress(ntdll, "NtQueryPerformanceCounter");

    // ---------- anchors ----------
    initialTime32 = initialOffset32 = realGetTickCount();
    initialTime64 = initialOffset64 = realGetTickCount64();

    LARGE_INTEGER q{};
    realQPC(&q);
    initialTimeQPC = initialOffsetQPC = q.QuadPart;

    LARGE_INTEGER s{};
    realNtQuerySystemTime(&s);
    initialTimeSys = initialOffsetSys = s.QuadPart;

    // ---------- hooks ----------
    MH_Initialize();

    MH_CreateHook(realGetTickCount, GetTickCount_Hook, (void**)&realGetTickCount);
    MH_CreateHook(realGetTickCount64, GetTickCount64_Hook, (void**)&realGetTickCount64);
    MH_CreateHook(realTimeGetTime, timeGetTime_Hook, (void**)&realTimeGetTime);
    MH_CreateHook(realQPC, QPC_Hook, (void**)&realQPC);
    //MH_CreateHook(realNtQuerySystemTime, NtQuerySystemTime_Hook, (void**)&realNtQuerySystemTime);
    //MH_CreateHook(realNtQueryPerformanceCounter, NtQueryPerformanceCounter_Hook, (void**)&realNtQueryPerformanceCounter);

    MH_EnableHook(MH_ALL_HOOKS);

    // ---------- watcher ----------
    std::thread(Watcher).detach();
}
