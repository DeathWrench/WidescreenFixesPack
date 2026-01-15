#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <thread>

namespace fs = std::filesystem;

// =======================================================
// Globals
// =======================================================

static std::atomic<bool> g_BackupThreadRunning{ false };
static HANDLE g_BackupDirHandle = INVALID_HANDLE_VALUE;

// =======================================================
// Backup logic
// =======================================================

static void BackupSavedGames()
{
    char userProfile[MAX_PATH]{};
    if (!GetEnvironmentVariableA("USERPROFILE", userProfile, MAX_PATH))
        return;

    fs::path source = fs::path(userProfile) / "Documents/TCNYC/Saved Games";
    fs::path backupRoot = fs::path(userProfile) / "Documents/TCNYC/Backups";

    if (!fs::exists(source))
        return;

    fs::create_directories(backupRoot);

    SYSTEMTIME st;
    GetLocalTime(&st);

    char folderName[64];
    sprintf_s(
        folderName,
        "SavedGames_%04d-%02d-%02d_%02d-%02d-%02d",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond
    );

    fs::path backupDest = backupRoot / folderName;
    fs::create_directories(backupDest);

    fs::copy(
        source,
        backupDest,
        fs::copy_options::recursive | fs::copy_options::overwrite_existing
    );
}

// =======================================================
// Directory watcher thread
// =======================================================

static void SavedGamesWatcherThread()
{
    char userProfile[MAX_PATH]{};
    if (!GetEnvironmentVariableA("USERPROFILE", userProfile, MAX_PATH))
        return;

    fs::path watchPath = fs::path(userProfile) / "Documents/TCNYC/Saved Games";
    if (!fs::exists(watchPath))
        return;

    g_BackupDirHandle = CreateFileW(
        watchPath.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr
    );

    if (g_BackupDirHandle == INVALID_HANDLE_VALUE)
        return;

    BYTE buffer[4096];
    DWORD bytesReturned;

    while (g_BackupThreadRunning)
    {
        if (ReadDirectoryChangesW(
            g_BackupDirHandle,
            buffer,
            sizeof(buffer),
            TRUE, // recursive
            FILE_NOTIFY_CHANGE_FILE_NAME |
            FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_LAST_WRITE |
            FILE_NOTIFY_CHANGE_SIZE,
            &bytesReturned,
            nullptr,
            nullptr))
        {
            // Any change = full snapshot
            BackupSavedGames();

            // Simple debounce so rapid writes don't spam backups
            Sleep(1000);
        }
    }

    CloseHandle(g_BackupDirHandle);
    g_BackupDirHandle = INVALID_HANDLE_VALUE;
}

// =======================================================
// DLL entry
// =======================================================

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_BackupThreadRunning = true;
        std::thread(SavedGamesWatcherThread).detach();
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        g_BackupThreadRunning = false;

        if (g_BackupDirHandle != INVALID_HANDLE_VALUE)
            CancelIoEx(g_BackupDirHandle, nullptr);
    }
    return TRUE;
}
