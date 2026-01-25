#include <Windows.h>

HWND g_hwnd = nullptr;
WNDPROC g_OriginalWndProc = nullptr;

// Hooked WndProc
LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE)
            return 0;
        break;

    case WM_ACTIVATEAPP:
        if (wParam == FALSE)
            return 0;
        break;
    }

    return CallWindowProc(g_OriginalWndProc, hwnd, uMsg, wParam, lParam);
}

// Poll for a window belonging to this process
DWORD WINAPI HookThread(LPVOID)
{
    DWORD pid = GetCurrentProcessId();
    while (!g_hwnd)
    {
        HWND hwnd = GetTopWindow(nullptr);
        while (hwnd)
        {
            DWORD winPID;
            GetWindowThreadProcessId(hwnd, &winPID);
            if (winPID == pid && IsWindowVisible(hwnd) && GetParent(hwnd) == nullptr)
            {
                g_hwnd = hwnd;
                // Remove borders (windowed)
                LONG style = GetWindowLong(g_hwnd, GWL_STYLE);
                style |= WS_OVERLAPPEDWINDOW;    // add resizable border
                style &= ~(WS_POPUP | WS_CAPTION); // remove fullscreen style if any
                SetWindowLong(g_hwnd, GWL_STYLE, style);
                g_OriginalWndProc = (WNDPROC)SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
                break;
            }
            hwnd = GetNextWindow(hwnd, GW_HWNDNEXT);
        }
        Sleep(50);
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        CreateThread(nullptr, 0, HookThread, nullptr, 0, nullptr);
        break;

    case DLL_PROCESS_DETACH:
        if (g_hwnd && g_OriginalWndProc)
            SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_OriginalWndProc);
        break;
    }
    return TRUE;
}
