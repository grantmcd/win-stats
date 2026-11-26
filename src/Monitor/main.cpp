#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "metrics.h"
#include "tray.h"
#include <filesystem>
#include <string>
#include <cstdio>

namespace {
constexpr wchar_t windowClass[] = L"WinStats.Monitor.1";
constexpr wchar_t settingsKey[] = L"Software\\WinStats\\Prototype";
constexpr UINT trayMessage = WM_APP + 1, openDetails = WM_APP + 2;
enum Command { Details = 1, Strip, Cpu, Ram, Net, CpuIcon, MemoryIcon, Retry, Quit };
UINT taskbarCreated;
HWND mainWindow;
HANDLE mapping, guard, stripProcess, detailsProcess;
DWORD detailsPid;
Snapshot sample;
Snapshot* shared;
Sampler sampler;
Tray tray;
bool memoryIcon, stripFailed;
unsigned restarts;
ULONGLONG stripStarted, retryAfter;
std::filesystem::path displayExe;

DWORD setting(const wchar_t* name, DWORD fallback) {
    DWORD value, bytes = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, settingsKey, name, RRF_RT_REG_DWORD, nullptr, &value, &bytes)
        == ERROR_SUCCESS ? value : fallback;
}

void save(const wchar_t* name, DWORD value) {
    RegSetKeyValueW(HKEY_CURRENT_USER, settingsKey, name, REG_DWORD, &value, sizeof(value));
}

void publish() {
    const auto lock = WaitForSingleObject(guard, 20);
    if (lock == WAIT_OBJECT_0 || lock == WAIT_ABANDONED) {
        *shared = sample;
        ReleaseMutex(guard);
    }
}

HANDLE launch(const wchar_t* mode, DWORD* pid = nullptr) {
    auto cmd = L"\"" + displayExe.wstring() + L"\" " + mode + L" " +
               std::to_wstring(GetCurrentProcessId()) + L" " + std::to_wstring(reinterpret_cast<UINT_PTR>(mainWindow));
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(displayExe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        displayExe.parent_path().c_str(), &startup, &child)) return nullptr;
    CloseHandle(child.hThread);
    if (pid) *pid = child.dwProcessId;
    return child.hProcess;
}

void reap(HANDLE& child) {
    if (child && WaitForSingleObject(child, 0) == WAIT_OBJECT_0) {
        CloseHandle(child);
        child = nullptr;
    }
}

void maintainStrip() {
    const auto now = GetTickCount64();
    if (stripProcess && WaitForSingleObject(stripProcess, 0) == WAIT_OBJECT_0) {
        DWORD code = 0;
        GetExitCodeProcess(stripProcess, &code);
        reap(stripProcess);
        if (now - stripStarted > 60000) restarts = 0;
        if (sample.stripEnabled && code != 0) {
            if (code == 21 && ++restarts <= 3) retryAfter = now + 1500;
            else stripFailed = true;
        }
    }
    if (sample.stripEnabled && !stripProcess && !stripFailed && now >= retryAfter) {
        stripProcess = launch(L"--strip");
        stripStarted = now;
        stripFailed = !stripProcess;
    }
}

void details() {
    reap(detailsProcess);
    if (detailsProcess) {
        EnumWindows([](HWND w, LPARAM id) -> BOOL {
            DWORD pid = 0;
            GetWindowThreadProcessId(w, &pid);
            if (pid != static_cast<DWORD>(id) || !IsWindowVisible(w)) return TRUE;
            if (IsIconic(w)) ShowWindow(w, SW_RESTORE);
            SetForegroundWindow(w);
            return FALSE;
        }, detailsPid);
        return;
    }
    detailsProcess = launch(L"--details", &detailsPid);
    if (!detailsProcess) MessageBoxW(mainWindow, L"Extract the complete build folder, including the Display folder.", L"Win Stats", MB_ICONERROR);
}

void menu(POINT point) {
    HMENU popup = CreatePopupMenu();
    auto item = [&](UINT id, const wchar_t* text, bool checked = false) {
        AppendMenuW(popup, MF_STRING | (checked ? MF_CHECKED : 0), id, text);
    };
    item(Details, L"Open live metrics");
    item(Strip, L"Show taskbar strip", sample.stripEnabled);
    if (stripFailed) item(Retry, L"Retry taskbar attachment");
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    item(Cpu, L"CPU in strip", sample.visibleMetrics & 1);
    item(Ram, L"Memory in strip", sample.visibleMetrics & 2);
    item(Net, L"Network in strip", sample.visibleMetrics & 4);
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    item(CpuIcon, L"Tray gauge: CPU", !memoryIcon);
    item(MemoryIcon, L"Tray gauge: memory", memoryIcon);
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    item(Quit, L"Exit Win Stats");
    SetForegroundWindow(mainWindow);
    const auto command = TrackPopupMenu(popup, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, mainWindow, nullptr);
    DestroyMenu(popup);
    PostMessageW(mainWindow, WM_NULL, 0, 0);
    switch (command) {
    case Details: details(); break;
    case Retry: case Strip:
        sample.stripEnabled = command == Retry || !sample.stripEnabled;
        restarts = 0; stripFailed = false; retryAfter = 0;
        save(L"ShowStrip", sample.stripEnabled);
        break;
    case Cpu: case Ram: case Net: {
        const auto next = sample.visibleMetrics ^ (1u << (command - Cpu));
        if (next) { sample.visibleMetrics = next; save(L"StripMetrics", next); }
        break;
    }
    case CpuIcon: case MemoryIcon:
        memoryIcon = command == MemoryIcon;
        save(L"MemoryIcon", memoryIcon);
        break;
    case Quit: DestroyWindow(mainWindow); return;
    }
    publish(); tray.update(sample, memoryIcon); maintainStrip();
}

LRESULT CALLBACK messages(HWND window, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == taskbarCreated && shared) {
        tray.explorerRestarted(); tray.update(sample, memoryIcon); return 0;
    }
    switch (msg) {
    case WM_TIMER:
        sampler.sample(sample); publish(); tray.update(sample, memoryIcon); maintainStrip(); return 0;
    case WM_POWERBROADCAST:
        if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) sampler.reset();
        return TRUE;
    case openDetails: details(); return 0;
    case trayMessage:
        switch (LOWORD(lParam)) {
        case NIN_SELECT: case NIN_KEYSELECT: details(); break;
        case WM_CONTEXTMENU: {
            POINT p{static_cast<short>(LOWORD(wParam)), static_cast<short>(HIWORD(wParam))};
            if (p.x == -1 && p.y == -1) GetCursorPos(&p);
            menu(p); break;
        }}
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, msg, wParam, lParam);
}

int selfTest() {
    sampler.sample(sample);
    Sleep(1100);
    sampler.sample(sample);
    return sample.memory >= 0 && sample.memory <= 100 && sample.totalMemory > 0 &&
           sample.cpu >= 0 && sample.cpu <= 100 ? 0 : 1;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR args, int) {
    const bool testing = std::wstring_view(args) == L"--self-test";
    if (testing && selfTest() != 0) return 1;
    HANDLE single = CreateMutexW(nullptr, FALSE, testing ? nullptr : L"Local\\WinStats.Monitor.Instance");
    if (!single) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (auto window = FindWindowW(windowClass, nullptr)) PostMessageW(window, openDetails, 0, 0);
        return 0;
    }
    wchar_t module[32768]{};
    if (!GetModuleFileNameW(nullptr, module, ARRAYSIZE(module))) return 1;
    displayExe = std::filesystem::path(module).parent_path() / L"Display" / L"WinStats.Display.exe";
    const auto prefix = L"Local\\WinStats." + std::to_wstring(GetCurrentProcessId());
    guard = CreateMutexW(nullptr, FALSE, (prefix + L".Guard").c_str());
    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Snapshot), (prefix + L".Data").c_str());
    if (!guard || !mapping) return 1;
    shared = static_cast<Snapshot*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(Snapshot)));
    if (!shared) return 1;
    if (testing) {
        publish();
        HANDLE display = launch(L"--smoke-test");
        if (!display) return 2;
        DWORD code = 3;
        if (WaitForSingleObject(display, 30000) == WAIT_OBJECT_0) GetExitCodeProcess(display, &code);
        CloseHandle(display);
        return static_cast<int>(code);
    }
    sample.stripEnabled = setting(L"ShowStrip", 1) != 0;
    sample.visibleMetrics = setting(L"StripMetrics", 7) & 7;
    if (!sample.visibleMetrics) sample.visibleMetrics = 7;
    memoryIcon = setting(L"MemoryIcon", 0) != 0;
    WNDCLASSW wc{};
    wc.hInstance = instance; wc.lpfnWndProc = messages; wc.lpszClassName = windowClass;
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (!RegisterClassW(&wc)) return 1;
    mainWindow = CreateWindowW(windowClass, L"Win Stats Monitor", WS_OVERLAPPED,
                               0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!mainWindow) return 1;
    tray.initialize(mainWindow, trayMessage);
    sampler.sample(sample); publish(); tray.update(sample, memoryIcon);
    if (!SetTimer(mainWindow, 1, 1000, nullptr)) return 1;
    maintainStrip();
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    UnmapViewOfFile(shared);
    CloseHandle(mapping); CloseHandle(guard); CloseHandle(single);
    if (stripProcess) CloseHandle(stripProcess);
    if (detailsProcess) CloseHandle(detailsProcess);
    return 0;
}
