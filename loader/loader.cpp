#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <psapi.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// kAzeware loader 1.1.7
//  - GUI subsystem: a double-click runs with no console window.
//  - In-place: this executable does not copy itself, does not spawn a random
//    %TEMP% image and does not request elevation. It injects kAzeware.dll from
//    its own directory and exits. The two-file layout (loader + DLL) follows
//    the PKC loader idea without the dropper-style self-copy stage.
//  - Live client: LoadLibraryW from the loader directory is attempted first.
//    If Trusted Mode rejects it, the DLL is staged below the Windows temporary
//    directory and kernelbase!LoadLibraryExW's entry is opened for the single
//    load call, then written back immediately. Game code is not patched
//    permanently.
//  - load/unload development verbs print to an inherited console when present.

namespace {
    constexpr const wchar_t* kDllName = L"kAzeware.dll";
    constexpr const wchar_t* kLogName = L"kAzeware-loader.log";
    constexpr const wchar_t* kStagingPrefix = L"kaze-";

    struct Handle {
        HANDLE h = nullptr;
        Handle() = default;

        explicit Handle(HANDLE value) : h(value) {}

        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;

        Handle(Handle&& other) noexcept : h(other.h) {
            other.h = nullptr;
        }

        Handle& operator=(Handle&& other) noexcept {
            if (this != &other) {
                if (h && h != INVALID_HANDLE_VALUE)
                    CloseHandle(h);
                h = other.h;
                other.h = nullptr;
            }
            return *this;
        }

        ~Handle() {
            if (h && h != INVALID_HANDLE_VALUE)
                CloseHandle(h);
        }

        operator HANDLE() const {
            return h;
        }

        explicit operator bool() const {
            return h && h != INVALID_HANDLE_VALUE;
        }
    };

    struct Module {
        std::uintptr_t base = 0;
        std::uintptr_t size = 0;
        std::filesystem::path path;
    };

    std::string ToUtf8(std::wstring_view text) {
        if (text.empty())
            return {};
        const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0)
            return {};
        std::string bytes(std::size_t(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), bytes.data(), size, nullptr, nullptr);
        return bytes;
    }

    std::wstring Widen(std::string_view text) {
        if (text.empty())
            return {};
        const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
        if (size <= 0)
            return {};
        std::wstring wide(std::size_t(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), wide.data(), size);
        return wide;
    }

    void Say(std::wstring_view text, bool error = false) {
        const auto handle = GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
        if (!handle || handle == INVALID_HANDLE_VALUE)
            return;
        DWORD mode = 0;
        if (GetConsoleMode(handle, &mode)) {
            DWORD written = 0;
            WriteConsoleW(handle, text.data(), DWORD(text.size()), &written, nullptr);
            return;
        }
        const auto bytes = ToUtf8(text);
        DWORD written = 0;
        ::WriteFile(handle, bytes.data(), DWORD(bytes.size()), &written, nullptr);
    }

    // A GUI subsystem process started from cmd or a pipe has no console of its own.
    void PrepareOutput() {
        const auto out = GetStdHandle(STD_OUTPUT_HANDLE);
        if (out && out != INVALID_HANDLE_VALUE)
            return;
        if (!AttachConsole(ATTACH_PARENT_PROCESS))
            return;
        const auto console = CreateFileW(L"CONOUT$",
                                         GENERIC_READ | GENERIC_WRITE,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                                         nullptr,
                                         OPEN_EXISTING,
                                         0,
                                         nullptr);
        if (console == INVALID_HANDLE_VALUE)
            return;
        SetStdHandle(STD_OUTPUT_HANDLE, console);
        SetStdHandle(STD_ERROR_HANDLE, console);
    }

    // Run tag is only diagnostics; it also names the staging folder. It is not a
    // security boundary and does not copy the loader image.
    std::wstring RunTag() {
        wchar_t buffer[32]{};
        swprintf_s(buffer,
                   L"%08x-%04x",
                   static_cast<unsigned>(GetTickCount64()),
                   static_cast<unsigned>(GetCurrentProcessId() & 0xffff));
        return buffer;
    }

    class Log {
        std::filesystem::path path_;
        std::wstring tag_;

    public:
        Log(std::filesystem::path path, std::wstring tag) : path_(std::move(path)), tag_(std::move(tag)) {}

        void Line(std::wstring_view text) const {
            SYSTEMTIME now{};
            GetLocalTime(&now);
            wchar_t stamp[32]{};
            swprintf_s(stamp,
                       L"%04u-%02u-%02u %02u:%02u:%02u",
                       now.wYear,
                       now.wMonth,
                       now.wDay,
                       now.wHour,
                       now.wMinute,
                       now.wSecond);
            const std::wstring row = std::wstring(stamp) + L" [" + tag_ + L"] " + std::wstring(text) + L"\r\n";
            const auto bytes = ToUtf8(row);
            std::ofstream file(path_, std::ios::binary | std::ios::app);
            file.write(bytes.data(), std::streamsize(bytes.size()));
        }
    };

    std::vector<DWORD> Games() {
        std::vector<DWORD> out;
        Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot.h, &entry)) {
            do {
                if (!_wcsicmp(entry.szExeFile, L"cs2.exe"))
                    out.push_back(entry.th32ProcessID);
            } while (Process32NextW(snapshot.h, &entry));
        }
        return out;
    }

    Module FindModule(DWORD pid, const wchar_t* name) {
        Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)};
        MODULEENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Module32FirstW(snapshot.h, &entry)) {
            do {
                if (_wcsicmp(entry.szModule, name))
                    continue;
                Handle process{OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid)};
                wchar_t path[32768]{};
                const auto count =
                    process ? GetModuleFileNameExW(process.h, reinterpret_cast<HMODULE>(entry.modBaseAddr), path, 32768)
                            : 0;
                if (!count || count >= 32768)
                    return {reinterpret_cast<std::uintptr_t>(entry.modBaseAddr), entry.modBaseSize, {}};
                return {reinterpret_cast<std::uintptr_t>(entry.modBaseAddr),
                        entry.modBaseSize,
                        std::filesystem::path(path)};
            } while (Module32NextW(snapshot.h, &entry));
        }
        return {};
    }

    DWORD RunRemote(HANDLE process, std::uintptr_t address, void* argument) {
        Handle thread{CreateRemoteThread(
            process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(address), argument, 0, nullptr)};
        if (!thread)
            throw std::runtime_error("cannot create loader thread");
        if (WaitForSingleObject(thread.h, 10000) != WAIT_OBJECT_0)
            throw std::runtime_error("loader thread timed out; module retained for inspection");
        DWORD result = 0;
        if (!GetExitCodeThread(thread.h, &result))
            throw std::runtime_error("cannot read loader result");
        return result;
    }

    std::uintptr_t SystemFunction(DWORD pid, const char* name, const wchar_t* module = L"kernel32.dll") {
        auto local = GetModuleHandleW(module);
        const auto function = local ? GetProcAddress(local, name) : nullptr;
        HMODULE actual = nullptr;
        if (!function ||
            !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(function),
                                &actual))
            throw std::runtime_error("system export unavailable");
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(actual, path, MAX_PATH);
        const auto remote = FindModule(pid, std::filesystem::path(path).filename().c_str());
        if (!remote.base)
            throw std::runtime_error("remote system module unavailable");
        return remote.base + reinterpret_cast<std::uintptr_t>(function) - reinterpret_cast<std::uintptr_t>(actual);
    }

    std::uintptr_t Export(const Module& module, const char* name) {
        auto local = LoadLibraryExW(module.path.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
        if (!local)
            throw std::runtime_error("cannot map kAzeware export table; win32=" + std::to_string(GetLastError()));
        const auto function = GetProcAddress(local, name);
        const auto offset = reinterpret_cast<std::uintptr_t>(function) - reinterpret_cast<std::uintptr_t>(local);
        FreeLibrary(local);
        if (!function)
            throw std::runtime_error("kAzeware export missing");
        return module.base + offset;
    }

    std::filesystem::path WindowsTempRoot() {
        wchar_t windows[MAX_PATH]{};
        if (!GetWindowsDirectoryW(windows, MAX_PATH))
            return {};
        return std::filesystem::path(windows) / L"Temp";
    }

    // Removes staging folders from earlier runs. A folder whose DLL is still
    // mapped cannot be removed and is skipped until the game exits.
    void CleanStaleStaging() {
        const auto root = WindowsTempRoot();
        if (root.empty())
            return;
        std::error_code error;
        std::filesystem::directory_iterator it(root, error);
        const std::filesystem::directory_iterator end;
        while (!error && it != end) {
            const auto entry = *it;
            it.increment(error);
            std::error_code local;
            if (!entry.is_directory(local))
                continue;
            if (entry.path().filename().wstring().rfind(kStagingPrefix, 0) != 0)
                continue;
            std::filesystem::remove_all(entry.path(), local);
        }
    }

    // ------------------------------------------------------ module hiding ---
    std::uintptr_t RemotePeb(HANDLE process) {
        struct ProcessBasicInformation {
            LONG exit_status;
            void* peb;
            std::uintptr_t affinity_mask, base_priority, unique_process, inherited_from;
        } info{};

        const auto query = reinterpret_cast<LONG(NTAPI*)(HANDLE, ULONG, void*, ULONG, ULONG*)>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
        if (!query)
            return 0;
        ULONG returned = 0;
        if (query(process, 0, &info, sizeof(info), &returned) < 0)
            return 0;
        return reinterpret_cast<std::uintptr_t>(info.peb);
    }

    // Trusted Mode enumerates the PEB loader lists for unsigned/invalid modules.
    // Removing only the three list links keeps the mapped image alive while the
    // game's module walk stops seeing it. The loader hash table entry is left in
    // place; do not FreeLibrary a hidden module.
    bool HideModuleFromPeb(HANDLE process, std::uintptr_t base) {
        const auto peb = RemotePeb(process);
        if (!peb)
            return false;
        std::uintptr_t ldr = 0;
        if (!ReadProcessMemory(process, reinterpret_cast<const void*>(peb + 0x18), &ldr, sizeof(ldr), nullptr) || !ldr)
            return false;
        const std::uintptr_t head = ldr + 0x10;
        std::uintptr_t entry = 0;
        if (!ReadProcessMemory(process, reinterpret_cast<const void*>(head), &entry, sizeof(entry), nullptr) || !entry)
            return false;
        for (int i = 0; i < 512 && entry && entry != head; ++i) {
            std::uintptr_t dll_base = 0;
            if (!ReadProcessMemory(
                    process, reinterpret_cast<const void*>(entry + 0x30), &dll_base, sizeof(dll_base), nullptr))
                return false;
            if (dll_base == base)
                break;
            if (!ReadProcessMemory(process, reinterpret_cast<const void*>(entry), &entry, sizeof(entry), nullptr))
                return false;
        }
        if (!entry || entry == head)
            return false;
        for (int i = 0; i < 3; ++i) {
            const std::uintptr_t link = entry + i * 0x10;
            std::uintptr_t flink = 0, blink = 0;
            if (!ReadProcessMemory(process, reinterpret_cast<const void*>(link), &flink, sizeof(flink), nullptr) ||
                !ReadProcessMemory(process, reinterpret_cast<const void*>(link + 8), &blink, sizeof(blink), nullptr) ||
                !flink || !blink)
                continue;
            WriteProcessMemory(process, reinterpret_cast<void*>(blink), &flink, sizeof(flink), nullptr);
            WriteProcessMemory(process, reinterpret_cast<void*>(flink + 8), &blink, sizeof(blink), nullptr);
        }
        return true;
    }

    // A memory walker can still find a mapped image by its MZ/PE headers after the
    // PEB links are gone. Zero the header page after the loader has finished with
    // it; the mapped code/data and the exception table stay valid. Do not call
    // GetProcAddress/FreeLibrary on this module afterwards.
    bool EraseModuleHeaders(HANDLE process, std::uintptr_t base) {
        DWORD previous = 0;
        if (!VirtualProtectEx(process, reinterpret_cast<void*>(base), 0x1000, PAGE_READWRITE, &previous))
            return false;
        std::array<unsigned char, 0x1000> zeros{};
        SIZE_T written = 0;
        const bool okay =
            WriteProcessMemory(process, reinterpret_cast<void*>(base), zeros.data(), zeros.size(), &written) &&
            written == zeros.size();
        DWORD ignored = 0;
        VirtualProtectEx(process, reinterpret_cast<void*>(base), 0x1000, PAGE_READONLY, &ignored);
        FlushInstructionCache(process, reinterpret_cast<void*>(base), 0x1000);
        return okay;
    }

    // VAC's user-mode process monitor walks threads and resolves their start
    // addresses. kAzeware's only dedicated thread is a wait loop after Present is
    // installed; once it is running, terminating that thread removes the last
    // unbacked thread start from the process. Feature work stays on game threads.
    int TerminateModuleThreads(HANDLE process, DWORD pid, std::uintptr_t base, std::uintptr_t size) {
        if (!base || !size)
            return 0;
        using QueryFn = LONG(NTAPI*)(HANDLE, ULONG, void*, ULONG, PULONG);
        const auto query =
            reinterpret_cast<QueryFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
        if (!query)
            return 0;
        int killed = 0;
        Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)};
        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        if (!Thread32First(snapshot.h, &entry))
            return 0;
        do {
            if (entry.th32OwnerProcessID != pid)
                continue;
            Handle thread{OpenThread(THREAD_QUERY_INFORMATION | THREAD_QUERY_LIMITED_INFORMATION | THREAD_TERMINATE,
                                     FALSE,
                                     entry.th32ThreadID)};
            if (!thread)
                continue;
            std::uintptr_t start = 0;
            ULONG returned = 0;
            if (query(thread.h, 9 /* ThreadQuerySetWin32StartAddress */, &start, sizeof(start), &returned) < 0)
                continue;
            if (start >= base && start < base + size && TerminateThread(thread.h, 0))
                ++killed;
        } while (Thread32Next(snapshot.h, &entry));
        return killed;
    }

    // ------------------------------------------------------ trusted-mode window ---
    struct LoadOutcome {
        std::uintptr_t load = 0;
        std::uintptr_t module = 0;
        std::uintptr_t module_size = 0;
        bool staged = false;
        bool window = false;
        bool restored = true;
        bool hidden = false;
        bool erased = false;
        std::filesystem::path staged_dll;
        std::wstring note;
    };

    struct RedirectWindow {
        std::uintptr_t address = 0;
        std::array<unsigned char, 5> original{};
        std::array<unsigned char, 5> patched{};
        bool prepared = false;
        bool opened = false;
    };

    bool PrepareRedirectWindow(HANDLE process, DWORD pid, RedirectWindow& window) {
        const auto remote_base = FindModule(pid, L"kernelbase.dll").base;
        HMODULE local = GetModuleHandleW(L"kernelbase.dll");
        if (!remote_base || !local)
            return false;
        const auto function = reinterpret_cast<std::uintptr_t>(GetProcAddress(local, "LoadLibraryExW"));
        if (!function)
            return false;
        window.address = remote_base + (function - reinterpret_cast<std::uintptr_t>(local));
        std::array<unsigned char, 8> remote{}, ours{};
        std::memcpy(ours.data(), reinterpret_cast<const void*>(function), ours.size());
        if (!ReadProcessMemory(
                process, reinterpret_cast<const void*>(window.address), remote.data(), remote.size(), nullptr))
            return false;
        if (remote[0] != 0xe9 || ours[0] == 0xe9)
            return false;
        if (!std::equal(remote.begin() + 5, remote.end(), ours.begin() + 5))
            return false;
        std::copy(remote.begin(), remote.begin() + 5, window.patched.begin());
        std::copy(ours.begin(), ours.begin() + 5, window.original.begin());
        window.prepared = true;
        return true;
    }

    bool WriteCode(HANDLE process, std::uintptr_t address, const void* bytes) {
        DWORD previous = 0;
        if (!VirtualProtectEx(process, reinterpret_cast<void*>(address), 5, PAGE_EXECUTE_READWRITE, &previous))
            return false;
        const bool written = WriteProcessMemory(process, reinterpret_cast<void*>(address), bytes, 5, nullptr) != FALSE;
        DWORD ignored = 0;
        VirtualProtectEx(process, reinterpret_cast<void*>(address), 5, previous, &ignored);
        FlushInstructionCache(process, reinterpret_cast<void*>(address), 5);
        return written;
    }

    bool OpenRedirectWindow(HANDLE process, RedirectWindow& window) {
        if (!window.prepared)
            return false;
        window.opened = WriteCode(process, window.address, window.original.data());
        return window.opened;
    }

    bool CloseRedirectWindow(HANDLE process, RedirectWindow& window) {
        if (!window.opened)
            return true;
        const bool back = WriteCode(process, window.address, window.patched.data());
        window.opened = false;
        return back;
    }

    std::uintptr_t LoadRemote(HANDLE process, DWORD pid, const std::filesystem::path& dll, std::uintptr_t& load) {
        const auto bytes = (dll.native().size() + 1) * sizeof(wchar_t);
        auto* allocation = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!allocation)
            throw std::runtime_error("cannot allocate the remote path");

        struct Release {
            HANDLE process;
            void* allocation;

            ~Release() {
                VirtualFreeEx(process, allocation, 0, MEM_RELEASE);
            }
        } release{process, allocation};

        SIZE_T written = 0;
        if (!WriteProcessMemory(process, allocation, dll.c_str(), bytes, &written) || written != bytes)
            throw std::runtime_error("cannot write the remote path");
        load = RunRemote(process, SystemFunction(pid, "LoadLibraryW", L"kernelbase.dll"), allocation);
        return RunRemote(process, SystemFunction(pid, "GetModuleHandleW"), allocation);
    }

    void HideLoadedModule(HANDLE process, DWORD pid, LoadOutcome& outcome) {
        // RunRemote returns a DWORD exit code, so the HMODULE is truncated. Resolve
        // the real 64-bit base from the module table before unlinking the PEB entry.
        const auto module = FindModule(pid, kDllName);
        const std::uintptr_t base = module.base ? module.base : outcome.module;
        if (!base)
            return;
        outcome.module = base;
        outcome.module_size = module.size;
        outcome.hidden = HideModuleFromPeb(process, base);
        if (outcome.hidden)
            outcome.erased = EraseModuleHeaders(process, base);
    }

    LoadOutcome InjectLibrary(
        HANDLE process, DWORD pid, const std::filesystem::path& dll, const std::wstring& tag, bool allow_guard_patch) {
        LoadOutcome outcome;
        outcome.module = LoadRemote(process, pid, dll, outcome.load);
        if (outcome.module) {
            HideLoadedModule(process, pid, outcome);
            return outcome;
        }
        if (!allow_guard_patch) {
            outcome.note = L"Trusted Mode 已生效，直接加载被拒绝。请重启游戏并在主菜单运行 loader；"
                           L"--guard 可强制使用一次性入口回退（该行为可能被 Defender 记录）";
            return outcome;
        }
        const auto root = WindowsTempRoot();
        if (root.empty()) {
            outcome.note = L"暂存失败：找不到 Windows 目录";
            return outcome;
        }
        const auto directory = root / (std::wstring(kStagingPrefix) + std::to_wstring(pid) + L"-" + tag.substr(0, 8));
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            outcome.note = L"暂存失败：无法创建 " + directory.wstring();
            return outcome;
        }
        const auto staged = directory / kDllName;
        std::filesystem::copy_file(dll, staged, std::filesystem::copy_options::overwrite_existing, error);
        if (error) {
            outcome.note = L"暂存失败：无法复制到 " + staged.wstring();
            return outcome;
        }
        outcome.staged = true;
        outcome.staged_dll = staged;
        RedirectWindow window;
        if (PrepareRedirectWindow(process, pid, window))
            OpenRedirectWindow(process, window);
        outcome.window = window.opened;
        outcome.load = 0;
        outcome.module = LoadRemote(process, pid, staged, outcome.load);
        outcome.restored = CloseRedirectWindow(process, window);
        if (outcome.module)
            HideLoadedModule(process, pid, outcome);
        return outcome;
    }

    // ---------------------------------------------------------------- commands ---
    HANDLE OpenGame(DWORD pid) {
        HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                         PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE,
                                     FALSE,
                                     pid);
        if (!process)
            throw std::runtime_error(ToUtf8(L"无法打开 cs2.exe（win32=" + std::to_wstring(GetLastError()) + L"）"));
        return process;
    }

    std::filesystem::path ResolveDll(const std::filesystem::path& home) {
        const auto dll = home / kDllName;
        if (!std::filesystem::is_regular_file(dll))
            throw std::runtime_error(ToUtf8(L"找不到 " + dll.wstring() + L"（loader 与 kAzeware.dll 需放在同一目录）"));
        return dll;
    }

    int CmdLoad(DWORD pid, const std::filesystem::path& home, bool allow_guard_patch) {
        if (FindModule(pid, kDllName).base)
            throw std::runtime_error("kAzeware.dll is already loaded");
        const auto dll = ResolveDll(home);
        Handle process{OpenGame(pid)};
        CleanStaleStaging();
        const auto tag = RunTag();
        const auto outcome = InjectLibrary(process, pid, dll, tag, allow_guard_patch);
        if (!outcome.module) {
            std::wstring reason = L"注入未生效：模块不在游戏内";
            if (!outcome.note.empty())
                reason += L"；" + outcome.note;
            throw std::runtime_error(ToUtf8(reason));
        }
        if (outcome.staged)
            Say(L"load: staged at " + outcome.staged_dll.wstring() + L"\n");
        else
            Say(L"load: direct from " + dll.wstring() + L"\n");
        Say(outcome.hidden ? L"load: module removed from the PEB loader lists\n"
                           : L"warning: module is still visible to the PEB module walk\n",
            !outcome.hidden);
        if (outcome.erased)
            Say(L"load: PE headers erased from the mapped image\n");
        if (outcome.hidden) {
            Sleep(3700);
            const int killed = TerminateModuleThreads(process, pid, outcome.module, outcome.module_size);
            Say(L"load: terminated kAzeware threads: " + std::to_wstring(killed) + L"\n");
        }
        if (outcome.window)
            Say(outcome.restored ? L"load: guard entry opened once and restored\n"
                                 : L"warning: guard entry was not restored; restart the game\n",
                !outcome.restored);
        Say(L"kAzeware loaded (PID " + std::to_wstring(pid) + L").\n");
        return 0;
    }

    int CmdUnload(DWORD pid) {
        const auto remote = FindModule(pid, kDllName);
        if (!remote.base)
            throw std::runtime_error("kAzeware is not loaded, or it is PEB-hidden (restart the game to clear it)");
        Handle process{OpenGame(pid)};
        RunRemote(process.h, Export(remote, "RequestUnload"), nullptr);
        const auto deadline = GetTickCount64() + 20000;
        while (FindModule(pid, kDllName).base && GetTickCount64() < deadline)
            Sleep(50);
        if (FindModule(pid, kDllName).base)
            throw std::runtime_error("unload is still pending; module retained");
        const auto folder = remote.path.parent_path();
        if (folder.filename().wstring().rfind(kStagingPrefix, 0) == 0) {
            std::error_code error;
            std::filesystem::remove_all(folder, error);
        }
        Say(L"kAzeware stopped and unloaded.\n");
        return 0;
    }

    void PrintBoundary() {
        Say(L"kAzeware-loader load [CS2 pid] [--guard]\n"
            L"kAzeware-loader unload [CS2 pid]\n"
            L"Double-click without arguments: direct-load kAzeware.dll next to the loader.\n"
            L"--guard enables the staging + kernelbase entry-window fallback; it can be\n"
            L"recorded by Defender, so run the loader at the main menu instead when possible.\n");
    }

    int AutoRun(const std::filesystem::path& self, bool allow_guard_patch) {
        const auto home = self.parent_path();
        const auto tag = RunTag();
        const Log log(home / kLogName, tag);
        log.Line(allow_guard_patch ? L"开始（就地注入，允许入口回退）" : L"开始（就地注入，直接加载）");
        const auto games = Games();
        if (games.empty()) {
            log.Line(L"cs2.exe 未运行，已结束");
            return 3;
        }
        if (games.size() > 1) {
            log.Line(L"检测到 " + std::to_wstring(games.size()) + L" 个 cs2.exe，请在命令行指定 PID");
            return 2;
        }
        const DWORD pid = games.front();
        if (FindModule(pid, kDllName).base) {
            log.Line(L"kAzeware.dll 已在游戏内，无需重复注入");
            return 0;
        }
        try {
            const auto dll = ResolveDll(home);
            log.Line(L"kAzeware.dll：" + dll.wstring());
            Handle process{OpenGame(pid)};
            const auto outcome = InjectLibrary(process, pid, dll, tag, allow_guard_patch);
            wchar_t text[200]{};
            swprintf_s(text,
                       L"LoadLibraryW=0x%llx，GetModuleHandleW=0x%llx",
                       (unsigned long long)outcome.load,
                       (unsigned long long)outcome.module);
            log.Line(text);
            if (!outcome.module) {
                if (!outcome.note.empty())
                    log.Line(outcome.note);
                log.Line(L"注入未生效：模块不在游戏内");
                return 1;
            }
            if (!outcome.note.empty())
                log.Line(outcome.note);
            if (outcome.staged)
                log.Line(L"已从 Windows 暂存副本加载 " + outcome.staged_dll.wstring());
            else
                log.Line(L"已从 loader 同目录直接加载");
            if (outcome.window)
                log.Line(outcome.restored ? L"加载拦截入口已临时打开并已还原"
                                          : L"警告：加载拦截入口未能还原，建议立即重启游戏");
            log.Line(outcome.hidden ? L"模块已加载并从 PEB 模块列表隐藏"
                                    : L"警告：模块已加载，但仍出现在 PEB 模块列表");
            if (outcome.erased)
                log.Line(L"模块 PE 头已从内存镜像擦除");
            Sleep(1200);
            if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT) {
                log.Line(L"游戏进程在初始化阶段退出");
                return 1;
            }
            if (outcome.hidden) {
                Sleep(2500);
                const int killed = TerminateModuleThreads(process, pid, outcome.module, outcome.module_size);
                log.Line(L"kAzeware 线程清理数量：" + std::to_wstring(killed));
                if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT) {
                    log.Line(L"游戏进程在线程清理后退出");
                    return 1;
                }
            }
            log.Line(L"kAzeware 已运行（PID " + std::to_wstring(pid) + L"）");
            return 0;
        } catch (const std::exception& failure) {
            log.Line(L"注入失败：" + Widen(failure.what()));
            log.Line(L"若游戏以管理员运行，请右键以管理员身份运行 loader");
            return 1;
        }
    }

    int RunDev(const std::vector<std::wstring>& positional, const std::filesystem::path& home, bool allow_guard_patch) {
        const std::wstring& verb = positional[0];
        const auto games = Games();
        DWORD pid = 0;
        if (positional.size() >= 2) {
            const auto value = std::stoul(positional[1]);
            if (value > MAXDWORD)
                throw std::runtime_error("invalid pid");
            pid = DWORD(value);
        } else {
            if (games.size() != 1)
                throw std::runtime_error("exactly one CS2 process is required; supply pid explicitly");
            pid = games.front();
        }
        if (std::find(games.begin(), games.end(), pid) == games.end())
            throw std::runtime_error("selected pid is not cs2.exe");
        if (verb == L"load")
            return CmdLoad(pid, home, allow_guard_patch);
        if (verb == L"unload")
            return CmdUnload(pid);
        PrintBoundary();
        return 2;
    }
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int count = 0;
    auto* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments)
        return 1;
    std::vector<std::wstring> positional;
    bool allow_guard_patch = false;
    for (int i = 1; i < count; ++i) {
        if (!_wcsicmp(arguments[i], L"--guard"))
            allow_guard_patch = true;
        else
            positional.emplace_back(arguments[i]);
    }
    LocalFree(arguments);
    wchar_t self_path[32768]{};
    GetModuleFileNameW(nullptr, self_path, 32768);
    const std::filesystem::path self(self_path);
    if (positional.empty())
        return AutoRun(self, true); // auto mode falls back to the guard window when needed
    PrepareOutput();
    try {
        const std::wstring& verb = positional[0];
        if (verb == L"load" || verb == L"unload")
            return RunDev(positional, self.parent_path(), allow_guard_patch);
        PrintBoundary();
        Say(L"unknown command: " + verb + L"\n", true);
        return 2;
    } catch (const std::exception& failure) {
        Say(L"error: " + Widen(failure.what()) + L"\n", true);
        return 1;
    }
}
