#include "mornox/platform/process.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <memory>
#include <string>
#include <utility>

namespace mornox {

namespace internal {

struct ChildProcessState {
    HANDLE process_handle = nullptr;
    HANDLE thread_handle = nullptr;
    HANDLE stdin_write = nullptr;
    HANDLE stdout_read = nullptr;
    HANDLE stderr_read = nullptr;
    DWORD process_id = 0;
    std::optional<int> exit_code;
};

}

namespace {

internal::ChildProcessState& EnsureState(std::unique_ptr<internal::ChildProcessState>& state) {
    if (state == nullptr) {
        state = std::make_unique<internal::ChildProcessState>();
    }
    return *state;
}

std::wstring WideFromUtf8(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        return std::wstring(value.begin(), value.end());
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::wstring WideFromPath(const std::filesystem::path& path) {
    return path.empty() ? std::wstring() : path.wstring();
}

std::wstring QuoteArgument(const std::string& argument) {
    if (argument.empty()) {
        return L"\"\"";
    }

    const std::wstring value = WideFromUtf8(argument);
    bool needs_quotes = false;
    for (wchar_t ch : value) {
        if (ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L'"') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) {
        return value;
    }

    std::wstring result = L"\"";
    std::size_t backslashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(ch);
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(ch);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

std::wstring CommandLineFor(const CommandSpec& spec) {
    std::wstring command = QuoteArgument(spec.executable);
    for (const std::string& argument : spec.arguments) {
        command.push_back(L' ');
        command += QuoteArgument(argument);
    }
    return command;
}

void CloseHandleIfValid(HANDLE& handle) {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
    }
    handle = nullptr;
}

std::string ReadAvailable(HANDLE handle) {
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        return {};
    }

    std::string result;
    std::array<char, 4096> buffer{};
    while (true) {
        DWORD available = 0;
        if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr) || available == 0) {
            break;
        }

        DWORD read = 0;
        const DWORD to_read = available < buffer.size() ? available : static_cast<DWORD>(buffer.size());
        if (!ReadFile(handle, buffer.data(), to_read, &read, nullptr) || read == 0) {
            break;
        }
        result.append(buffer.data(), read);
    }
    return result;
}

std::string LastErrorMessage(const char* fallback) {
    const DWORD error = GetLastError();
    if (error == 0) {
        return fallback;
    }

    LPWSTR buffer = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);
    if (size == 0 || buffer == nullptr) {
        return fallback;
    }

    std::wstring wide(buffer, size);
    LocalFree(buffer);
    while (!wide.empty() && (wide.back() == L'\r' || wide.back() == L'\n')) {
        wide.pop_back();
    }

    const int utf8_size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (utf8_size <= 0) {
        return fallback;
    }
    std::string result(static_cast<std::size_t>(utf8_size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(), utf8_size, nullptr, nullptr);
    return result;
}

void CloseLocalHandles(HANDLE& stdin_read, HANDLE& stdin_write, HANDLE& stdout_read, HANDLE& stdout_write, HANDLE& stderr_read, HANDLE& stderr_write) {
    CloseHandleIfValid(stdin_read);
    CloseHandleIfValid(stdin_write);
    CloseHandleIfValid(stdout_read);
    CloseHandleIfValid(stdout_write);
    CloseHandleIfValid(stderr_read);
    CloseHandleIfValid(stderr_write);
}

}

ChildProcess::ChildProcess() = default;

ChildProcess::ChildProcess(ChildProcess&& other) noexcept
    : state_(std::move(other.state_)) {}

ChildProcess& ChildProcess::operator=(ChildProcess&& other) noexcept {
    if (this != &other) {
        Terminate();
        state_ = std::move(other.state_);
    }
    return *this;
}

ChildProcess::~ChildProcess() {
    Terminate();
}

bool ChildProcess::Start(const CommandSpec& spec, std::string* error_message) {
    Terminate();
    internal::ChildProcessState& state = EnsureState(state_);
    state.exit_code.reset();

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(SECURITY_ATTRIBUTES);
    security.bInheritHandle = TRUE;

    HANDLE stdin_read = nullptr;
    HANDLE stdin_write = nullptr;
    HANDLE stdout_read = nullptr;
    HANDLE stdout_write = nullptr;
    HANDLE stderr_read = nullptr;
    HANDLE stderr_write = nullptr;

    const bool pipes_created =
        CreatePipe(&stdin_read, &stdin_write, &security, 0) &&
        CreatePipe(&stdout_read, &stdout_write, &security, 0) &&
        CreatePipe(&stderr_read, &stderr_write, &security, 0);
    if (!pipes_created) {
        if (error_message != nullptr) {
            *error_message = LastErrorMessage("Failed to create process pipes");
        }
        CloseLocalHandles(stdin_read, stdin_write, stdout_read, stdout_write, stderr_read, stderr_write);
        return false;
    }

    SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(STARTUPINFOW);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = stdin_read;
    startup.hStdOutput = stdout_write;
    startup.hStdError = stderr_write;

    PROCESS_INFORMATION process{};
    std::wstring command_line = CommandLineFor(spec);
    std::wstring working_directory = WideFromPath(spec.working_directory);
    const BOOL created = CreateProcessW(
        nullptr,
        command_line.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        working_directory.empty() ? nullptr : working_directory.c_str(),
        &startup,
        &process);

    CloseHandleIfValid(stdin_read);
    CloseHandleIfValid(stdout_write);
    CloseHandleIfValid(stderr_write);

    if (!created) {
        if (error_message != nullptr) {
            *error_message = LastErrorMessage("Failed to start process");
        }
        CloseHandleIfValid(stdin_write);
        CloseHandleIfValid(stdout_read);
        CloseHandleIfValid(stderr_read);
        return false;
    }

    state.process_handle = process.hProcess;
    state.thread_handle = process.hThread;
    state.process_id = process.dwProcessId;
    state.stdin_write = stdin_write;
    state.stdout_read = stdout_read;
    state.stderr_read = stderr_read;
    return true;
}

bool ChildProcess::Running() const {
    if (state_ == nullptr || state_->process_handle == nullptr) {
        return false;
    }
    return WaitForSingleObject(state_->process_handle, 0) == WAIT_TIMEOUT;
}

std::optional<int> ChildProcess::TryWait() {
    if (state_ == nullptr || state_->process_handle == nullptr) {
        return state_ == nullptr ? std::nullopt : state_->exit_code;
    }
    const DWORD wait = WaitForSingleObject(state_->process_handle, 0);
    if (wait == WAIT_TIMEOUT) {
        return std::nullopt;
    }
    if (wait == WAIT_OBJECT_0) {
        DWORD exit_code = 0;
        if (GetExitCodeProcess(state_->process_handle, &exit_code)) {
            RememberExitStatus(static_cast<int>(exit_code));
        } else {
            state_->exit_code = -1;
        }
        CloseHandleIfValid(state_->process_handle);
        CloseHandleIfValid(state_->thread_handle);
        state_->process_id = 0;
        return state_->exit_code;
    }
    return std::nullopt;
}

int ChildProcess::Wait() {
    if (state_ == nullptr || state_->process_handle == nullptr) {
        return state_ == nullptr ? -1 : state_->exit_code.value_or(-1);
    }
    WaitForSingleObject(state_->process_handle, INFINITE);
    DWORD exit_code = 0;
    if (GetExitCodeProcess(state_->process_handle, &exit_code)) {
        RememberExitStatus(static_cast<int>(exit_code));
    } else {
        state_->exit_code = -1;
    }
    CloseHandleIfValid(state_->process_handle);
    CloseHandleIfValid(state_->thread_handle);
    state_->process_id = 0;
    return state_->exit_code.value_or(-1);
}

bool ChildProcess::WriteStdin(const std::string& text) {
    if (state_ == nullptr || state_->stdin_write == nullptr) {
        return false;
    }

    const char* data = text.data();
    std::size_t remaining = text.size();
    while (remaining > 0) {
        DWORD written = 0;
        const DWORD to_write = remaining > static_cast<std::size_t>(MAXDWORD) ? MAXDWORD : static_cast<DWORD>(remaining);
        if (!WriteFile(state_->stdin_write, data, to_write, &written, nullptr) || written == 0) {
            return false;
        }
        data += written;
        remaining -= written;
    }
    return true;
}

std::string ChildProcess::ReadStdoutAvailable() {
    return state_ == nullptr ? std::string() : ReadAvailable(state_->stdout_read);
}

std::string ChildProcess::ReadStderrAvailable() {
    return state_ == nullptr ? std::string() : ReadAvailable(state_->stderr_read);
}

void ChildProcess::Terminate() {
    if (state_ == nullptr) {
        return;
    }
    if (state_->process_handle != nullptr) {
        if (Running()) {
            TerminateProcess(state_->process_handle, 130);
        }
        WaitForSingleObject(state_->process_handle, INFINITE);
        DWORD exit_code = 0;
        if (GetExitCodeProcess(state_->process_handle, &exit_code)) {
            RememberExitStatus(static_cast<int>(exit_code));
        } else {
            state_->exit_code = 130;
        }
        CloseHandleIfValid(state_->process_handle);
        CloseHandleIfValid(state_->thread_handle);
        state_->process_id = 0;
    }
    ClosePipes();
}

std::optional<int> ChildProcess::ExitCode() const {
    return state_ == nullptr ? std::nullopt : state_->exit_code;
}

void ChildProcess::ClosePipes() {
    if (state_ == nullptr) {
        return;
    }
    CloseHandleIfValid(state_->stdin_write);
    CloseHandleIfValid(state_->stdout_read);
    CloseHandleIfValid(state_->stderr_read);
}

void ChildProcess::RememberExitStatus(int status) {
    internal::ChildProcessState& state = EnsureState(state_);
    state.exit_code = status;
}

}

#endif
