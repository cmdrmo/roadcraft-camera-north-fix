#include <windows.h>
#include <tlhelp32.h>
#include <commctrl.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
constexpr wchar_t WindowClassName[] = L"RoadCraftCameraNorthFixWindow";
constexpr wchar_t WindowTitle[] = L"RoadCraft Camera North Fix v0.1.0";
constexpr UINT_PTR UiTimer = 1;
constexpr std::uintptr_t HookRva = 0x009E6123;
constexpr std::uintptr_t RelayRva = 0x009E8561;
constexpr std::array<std::uint8_t, 8> ExpectedHook{0x49, 0x89, 0x46, 0x04, 0x41, 0xC6, 0x07, 0x01};
constexpr std::array<std::uint8_t, 14> ExpectedRelay{
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
constexpr std::array<std::uint8_t, 8> StateMarker{'O','Y','G','U','A','R','D','1'};
constexpr int ResourceId = 101;

enum ControlId : int {
    StartButton = 1001,
    StopButton = 1002,
    StatusText = 1003,
    ProcessText = 1004,
    StatsText = 1005,
    LogText = 1006,
};

struct State {
    std::uint32_t totalWrites;
    std::uint32_t rebasedWrites;
    std::uint32_t over20;
    std::uint32_t eventIndex;
    std::uint32_t maximumDeltaBits;
    std::uint32_t rebaseEnabled;
    std::uint32_t replacementBits;
    std::uint32_t reserved;
};
static_assert(sizeof(State) == 32);

struct Session {
    HANDLE process{};
    DWORD pid{};
    std::uintptr_t module{};
    std::uintptr_t hook{};
    std::uintptr_t relay{};
    void* remote{};
    std::size_t remoteSize{};
    std::size_t stateOffset{};
    State state{};
    std::uint32_t lastLoggedRebases{};
    bool active{};
};

HINSTANCE gInstance{};
HANDLE gSingleInstance{};
HWND gWindow{}, gStatus{}, gProcess{}, gStats{}, gLog{}, gStart{}, gStop{};
Session gSession{};
std::filesystem::path gLogPath;

std::wstring Timestamp() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%02u:%02u:%02u", time.wHour, time.wMinute, time.wSecond);
    return buffer;
}

std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

void AppendLog(const std::wstring& message) {
    const std::wstring line = L"[" + Timestamp() + L"] " + message + L"\r\n";
    if (gLog) {
        SendMessageW(gLog, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
        SendMessageW(gLog, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
        SendMessageW(gLog, EM_SCROLLCARET, 0, 0);
    }
    if (!gLogPath.empty()) {
        std::ofstream stream(gLogPath, std::ios::binary | std::ios::app);
        const auto utf8 = Utf8(line);
        stream.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    }
}

DWORD FindPid() {
    PROCESSENTRY32W entry{sizeof(entry)};
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    DWORD pid = 0;
    if (Process32FirstW(snapshot, &entry)) do {
        if (_wcsicmp(entry.szExeFile, L"Roadcraft - Retail.exe") == 0) {
            pid = entry.th32ProcessID;
            break;
        }
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return pid;
}

std::uintptr_t FindModule(DWORD pid, std::wstring* path = nullptr) {
    MODULEENTRY32W entry{sizeof(entry)};
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    std::uintptr_t base = 0;
    if (Module32FirstW(snapshot, &entry)) do {
        if (_wcsicmp(entry.szModule, L"Roadcraft - Retail.exe") == 0) {
            base = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
            if (path) *path = entry.szExePath;
            break;
        }
    } while (Module32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return base;
}

template<std::size_t N>
bool ReadExact(HANDLE process, std::uintptr_t address, std::array<std::uint8_t, N>& bytes) {
    SIZE_T read{};
    return ReadProcessMemory(process, reinterpret_cast<void*>(address), bytes.data(), N, &read) && read == N;
}

bool WriteExact(HANDLE process, std::uintptr_t address, const void* data, std::size_t size) {
    DWORD oldProtection{};
    if (!VirtualProtectEx(process, reinterpret_cast<void*>(address), size,
            PAGE_EXECUTE_READWRITE, &oldProtection)) return false;
    SIZE_T written{};
    const bool wrote = WriteProcessMemory(process, reinterpret_cast<void*>(address),
        data, size, &written) && written == size;
    FlushInstructionCache(process, reinterpret_cast<void*>(address), size);
    DWORD ignored{};
    const bool restored = VirtualProtectEx(process, reinterpret_cast<void*>(address),
        size, oldProtection, &ignored) != FALSE;
    return wrote && restored;
}

std::vector<std::uint8_t> LoadEmbeddedStub() {
    const HRSRC resource = FindResourceW(gInstance, MAKEINTRESOURCEW(ResourceId), RT_RCDATA);
    if (!resource) return {};
    const HGLOBAL loaded = LoadResource(gInstance, resource);
    if (!loaded) return {};
    const DWORD size = SizeofResource(gInstance, resource);
    const void* bytes = LockResource(loaded);
    if (!bytes || !size) return {};
    const auto* begin = static_cast<const std::uint8_t*>(bytes);
    return {begin, begin + size};
}

std::size_t FindStateOffset(const std::vector<std::uint8_t>& blob) {
    for (std::size_t i = 0; i + StateMarker.size() <= blob.size(); ++i)
        if (std::memcmp(blob.data() + i, StateMarker.data(), StateMarker.size()) == 0)
            return i + StateMarker.size();
    return static_cast<std::size_t>(-1);
}

bool ValidateBytes(HANDLE process, std::uintptr_t module) {
    std::array<std::uint8_t, 8> hook{};
    std::array<std::uint8_t, 14> relay{};
    return ReadExact(process, module + HookRva, hook) && hook == ExpectedHook &&
        ReadExact(process, module + RelayRva, relay) && relay == ExpectedRelay;
}

void UpdateButtons() {
    EnableWindow(gStart, !gSession.active);
    EnableWindow(gStop, gSession.active);
}

void ShowState(const State& state) {
    std::wostringstream text;
    text.setf(std::ios::fixed);
    text.precision(3);
    text << L"Writes: " << state.totalWrites
         << L"    Anomalies: " << state.over20
         << L"    Rebases: " << state.rebasedWrites
         << L"    Max raw jump: " << std::bit_cast<float>(state.maximumDeltaBits) << L"°";
    SetWindowTextW(gStats, text.str().c_str());
}

bool StartFix() {
    if (gSession.active) return true;
    const auto blob = LoadEmbeddedStub();
    const auto stateOffset = FindStateOffset(blob);
    if (blob.empty() || stateOffset == static_cast<std::size_t>(-1)) {
        AppendLog(L"Error: embedded fix resource is invalid.");
        return false;
    }

    const DWORD pid = FindPid();
    std::wstring gamePath;
    const auto module = pid ? FindModule(pid, &gamePath) : 0;
    if (!pid || !module) {
        AppendLog(L"RoadCraft was not found. Start the game first.");
        return false;
    }
    const HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ |
        PROCESS_VM_WRITE | PROCESS_VM_OPERATION | SYNCHRONIZE, FALSE, pid);
    if (!process) {
        AppendLog(L"Error: could not open the game process. Win32=" + std::to_wstring(GetLastError()));
        return false;
    }
    if (!ValidateBytes(process, module)) {
        AppendLog(L"Refused: game-version bytes or relay cave do not match.");
        CloseHandle(process);
        return false;
    }

    void* remote = VirtualAllocEx(process, nullptr, blob.size(), MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE);
    SIZE_T transferred{};
    if (!remote || !WriteProcessMemory(process, remote, blob.data(), blob.size(), &transferred) ||
        transferred != blob.size()) {
        AppendLog(L"Error: could not write the temporary fix code.");
        if (remote) VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }
    const std::uint32_t enabled = 1;
    if (!WriteProcessMemory(process, static_cast<std::uint8_t*>(remote) + stateOffset + 20,
            &enabled, sizeof(enabled), &transferred) || transferred != sizeof(enabled)) {
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        AppendLog(L"Error: could not enable canonical-rebase mode.");
        return false;
    }
    FlushInstructionCache(process, remote, blob.size());

    const auto hook = module + HookRva;
    const auto relay = module + RelayRva;
    std::array<std::uint8_t, 14> relayPatch{0xFF, 0x25, 0, 0, 0, 0};
    const auto remoteAddress = reinterpret_cast<std::uint64_t>(remote);
    std::memcpy(relayPatch.data() + 6, &remoteAddress, sizeof(remoteAddress));
    std::array<std::uint8_t, 8> hookPatch{0xE8, 0, 0, 0, 0, 0x90, 0x90, 0x90};
    const auto displacement = static_cast<std::int32_t>(
        static_cast<std::int64_t>(relay) - static_cast<std::int64_t>(hook + 5));
    std::memcpy(hookPatch.data() + 1, &displacement, sizeof(displacement));

    const bool relayInstalled = WriteExact(process, relay, relayPatch.data(), relayPatch.size());
    const bool hookInstalled = relayInstalled && WriteExact(process, hook, hookPatch.data(), hookPatch.size());
    if (!hookInstalled) {
        WriteExact(process, hook, ExpectedHook.data(), ExpectedHook.size());
        WriteExact(process, relay, ExpectedRelay.data(), ExpectedRelay.size());
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        AppendLog(L"Error: installation failed; rollback was attempted.");
        return false;
    }

    gSession = {process, pid, module, hook, relay, remote, blob.size(), stateOffset, {}, 0, true};
    SetWindowTextW(gStatus, L"Fix enabled (canonical rebase)");
    SetWindowTextW(gProcess, (L"PID " + std::to_wstring(pid) + L"    " + gamePath).c_str());
    ShowState({});
    UpdateButtons();
    AppendLog(L"Canonical-rebase fix installed. PID=" + std::to_wstring(pid));
    return true;
}

void ResetSessionUi(const wchar_t* status) {
    gSession = {};
    SetWindowTextW(gStatus, status);
    ShowState({});
    UpdateButtons();
}

bool StopFix(bool gameAlreadyExited = false) {
    if (!gSession.active) return true;
    if (gameAlreadyExited) {
        AppendLog(L"Game exited; session state was cleared.");
        CloseHandle(gSession.process);
        ResetSessionUi(L"Waiting for RoadCraft");
        return true;
    }

    const bool hookRestored = WriteExact(gSession.process, gSession.hook,
        ExpectedHook.data(), ExpectedHook.size());
    if (hookRestored) Sleep(30);
    State finalState{};
    SIZE_T read{};
    const bool stateRead = hookRestored && ReadProcessMemory(gSession.process,
        static_cast<std::uint8_t*>(gSession.remote) + gSession.stateOffset,
        &finalState, sizeof(finalState), &read) && read == sizeof(finalState);
    const bool relayRestored = hookRestored && WriteExact(gSession.process, gSession.relay,
        ExpectedRelay.data(), ExpectedRelay.size());
    const bool freed = relayRestored && VirtualFreeEx(gSession.process, gSession.remote, 0, MEM_RELEASE);
    if (!hookRestored || !relayRestored || !freed) {
        AppendLog(L"WARNING: incomplete restore. Keep this window open and exit RoadCraft.");
        SetWindowTextW(gStatus, L"Restore failed: exit RoadCraft first");
        return false;
    }

    if (stateRead) {
        AppendLog(L"Stopped and restored. writes=" + std::to_wstring(finalState.totalWrites) +
            L" anomalies=" + std::to_wstring(finalState.over20) +
            L" rebased=" + std::to_wstring(finalState.rebasedWrites));
    } else {
        AppendLog(L"Stopped and restored; final counters could not be read.");
    }
    CloseHandle(gSession.process);
    ResetSessionUi(L"Fix stopped; original bytes restored");
    return true;
}

void Poll() {
    if (gSession.active) {
        DWORD exitCode{};
        if (!GetExitCodeProcess(gSession.process, &exitCode) || exitCode != STILL_ACTIVE) {
            StopFix(true);
            return;
        }
        State state{};
        SIZE_T read{};
        if (ReadProcessMemory(gSession.process,
                static_cast<std::uint8_t*>(gSession.remote) + gSession.stateOffset,
                &state, sizeof(state), &read) && read == sizeof(state)) {
            gSession.state = state;
            ShowState(state);
            if (state.rebasedWrites != gSession.lastLoggedRebases) {
                const auto increase = state.rebasedWrites - gSession.lastLoggedRebases;
                gSession.lastLoggedRebases = state.rebasedWrites;
                AppendLog(L"Rebased " + std::to_wstring(increase) +
                    L" new event(s); total " + std::to_wstring(state.rebasedWrites));
            }
        }
        return;
    }

    const DWORD pid = FindPid();
    std::wstring path;
    const auto module = pid ? FindModule(pid, &path) : 0;
    if (!pid || !module) {
        SetWindowTextW(gProcess, L"RoadCraft not detected");
        SetWindowTextW(gStatus, L"Waiting for RoadCraft");
        return;
    }
    const HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    const bool valid = process && ValidateBytes(process, module);
    if (process) CloseHandle(process);
    SetWindowTextW(gProcess, (L"PID " + std::to_wstring(pid) + L"    " + path).c_str());
    SetWindowTextW(gStatus, valid ? L"Compatible game detected; ready to enable" :
        L"Game detected, but version bytes mismatch or are already patched");
}

void ApplyFont(HWND window) {
    const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        CreateWindowW(L"STATIC", L"RoadCraft Camera North Fix v0.1.0",
            WS_CHILD | WS_VISIBLE, 20, 16, 620, 28, window, nullptr, gInstance, nullptr);
        CreateWindowW(L"STATIC", L"Status:", WS_CHILD | WS_VISIBLE,
            20, 55, 55, 22, window, nullptr, gInstance, nullptr);
        gStatus = CreateWindowW(L"STATIC", L"Initializing", WS_CHILD | WS_VISIBLE,
            78, 55, 555, 22, window, reinterpret_cast<HMENU>(StatusText), gInstance, nullptr);
        CreateWindowW(L"STATIC", L"Process:", WS_CHILD | WS_VISIBLE,
            20, 83, 55, 22, window, nullptr, gInstance, nullptr);
        gProcess = CreateWindowW(L"STATIC", L"Not detected", WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS,
            78, 83, 555, 22, window, reinterpret_cast<HMENU>(ProcessText), gInstance, nullptr);
        gStart = CreateWindowW(L"BUTTON", L"Enable fix", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            20, 118, 135, 34, window, reinterpret_cast<HMENU>(StartButton), gInstance, nullptr);
        gStop = CreateWindowW(L"BUTTON", L"Stop and restore", WS_CHILD | WS_VISIBLE,
            166, 118, 135, 34, window, reinterpret_cast<HMENU>(StopButton), gInstance, nullptr);
        CreateWindowW(L"STATIC", L"Closing normally restores the patch. On failure, exit the game.",
            WS_CHILD | WS_VISIBLE, 320, 126, 315, 22, window, nullptr, gInstance, nullptr);
        gStats = CreateWindowW(L"STATIC", L"Writes: 0    Anomalies: 0    Rebases: 0    Max raw jump: 0°",
            WS_CHILD | WS_VISIBLE, 20, 167, 615, 24, window,
            reinterpret_cast<HMENU>(StatsText), gInstance, nullptr);
        CreateWindowW(L"STATIC", L"Runtime log:", WS_CHILD | WS_VISIBLE,
            20, 200, 100, 22, window, nullptr, gInstance, nullptr);
        gLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            20, 225, 615, 245, window, reinterpret_cast<HMENU>(LogText), gInstance, nullptr);
        EnumChildWindows(window, [](HWND child, LPARAM) -> BOOL { ApplyFont(child); return TRUE; }, 0);
        UpdateButtons();
        AppendLog(L"UI started. This build targets Steam build 23930923.");
        SetTimer(window, UiTimer, 1000, nullptr);
        Poll();
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == StartButton) StartFix();
        else if (LOWORD(wParam) == StopButton) StopFix();
        return 0;
    case WM_TIMER:
        if (wParam == UiTimer) Poll();
        return 0;
    case WM_CLOSE:
        if (!gSession.active || StopFix()) DestroyWindow(window);
        else MessageBoxW(window, L"Could not fully restore the game. Exit RoadCraft before closing this window.",
            L"RoadCraft Camera Fix", MB_ICONERROR | MB_OK);
        return 0;
    case WM_DESTROY:
        KillTimer(window, UiTimer);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

int SelfTest() {
    const auto blob = LoadEmbeddedStub();
    if (blob.empty() || FindStateOffset(blob) == static_cast<std::size_t>(-1)) return 20;
    const DWORD pid = FindPid();
    const auto module = pid ? FindModule(pid) : 0;
    if (!pid || !module) return 21;
    const HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) return 22;
    const bool valid = ValidateBytes(process, module);
    CloseHandle(process);
    return valid ? 0 : 23;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    gInstance = instance;
    if (wcsstr(GetCommandLineW(), L"--self-test")) return SelfTest();

    gSingleInstance = CreateMutexW(nullptr, FALSE, L"Local\\RoadCraftCameraNorthFix_v0_1_0");
    if (!gSingleInstance || GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"RoadCraft Camera North Fix is already running.",
            WindowTitle, MB_ICONINFORMATION | MB_OK);
        if (gSingleInstance) CloseHandle(gSingleInstance);
        return 3;
    }

    wchar_t ownPath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, ownPath, MAX_PATH);
    gLogPath = std::filesystem::path(ownPath).replace_extension(L".log");

    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = WindowClassName;
    windowClass.hIconSm = windowClass.hIcon;
    if (!RegisterClassExW(&windowClass)) return 1;

    gWindow = CreateWindowExW(0, WindowClassName, WindowTitle,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 675, 525, nullptr, nullptr, instance, nullptr);
    if (!gWindow) return 2;
    ShowWindow(gWindow, showCommand);
    UpdateWindow(gWindow);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
