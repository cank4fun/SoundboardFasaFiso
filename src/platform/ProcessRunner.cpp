#include "platform/ProcessRunner.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    class UniqueHandle final
    {
    public:
        UniqueHandle() = default;

        explicit UniqueHandle(const HANDLE handle) noexcept
            : handle_(handle)
        {
        }

        ~UniqueHandle()
        {
            Reset();
        }

        UniqueHandle(const UniqueHandle&) = delete;
        UniqueHandle& operator=(const UniqueHandle&) = delete;

        UniqueHandle(UniqueHandle&& other) noexcept
            : handle_(std::exchange(other.handle_, nullptr))
        {
        }

        UniqueHandle& operator=(UniqueHandle&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                handle_ = std::exchange(other.handle_, nullptr);
            }

            return *this;
        }

        HANDLE Get() const noexcept
        {
            return handle_;
        }

        HANDLE Release() noexcept
        {
            return std::exchange(handle_, nullptr);
        }

        void Reset(const HANDLE replacement = nullptr) noexcept
        {
            if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
            {
                CloseHandle(handle_);
            }

            handle_ = replacement;
        }

        explicit operator bool() const noexcept
        {
            return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
        }

    private:
        HANDLE handle_ = nullptr;
    };

    class AttributeList final
    {
    public:
        bool Initialize(
            HANDLE* inheritedHandles,
            const std::size_t handleCount,
            std::string& errorMessage
        )
        {
            SIZE_T bytes = 0;
            InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);

            if (bytes == 0)
            {
                errorMessage =
                    "Windows process attribute-list sizing failed. Error " +
                    std::to_string(GetLastError()) + ".";
                return false;
            }

            storage_.resize(bytes);
            list_ = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
                storage_.data()
            );

            if (InitializeProcThreadAttributeList(list_, 1, 0, &bytes) == FALSE)
            {
                errorMessage =
                    "Windows process attribute-list initialization failed. Error " +
                    std::to_string(GetLastError()) + ".";
                list_ = nullptr;
                return false;
            }

            initialized_ = true;

            if (UpdateProcThreadAttribute(
                    list_,
                    0,
                    PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                    inheritedHandles,
                    sizeof(HANDLE) * handleCount,
                    nullptr,
                    nullptr
                ) == FALSE)
            {
                errorMessage =
                    "Windows inherited-handle restriction failed. Error " +
                    std::to_string(GetLastError()) + ".";
                return false;
            }

            return true;
        }

        ~AttributeList()
        {
            if (initialized_)
            {
                DeleteProcThreadAttributeList(list_);
            }
        }

        LPPROC_THREAD_ATTRIBUTE_LIST Get() const noexcept
        {
            return list_;
        }

    private:
        std::vector<unsigned char> storage_;
        LPPROC_THREAD_ATTRIBUTE_LIST list_ = nullptr;
        bool initialized_ = false;
    };

    bool CreateCapturedPipe(
        UniqueHandle& readHandle,
        UniqueHandle& writeHandle,
        std::string& errorMessage
    )
    {
        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;

        HANDLE rawRead = nullptr;
        HANDLE rawWrite = nullptr;

        if (CreatePipe(
                &rawRead,
                &rawWrite,
                &security,
                0
            ) == FALSE)
        {
            errorMessage =
                "Windows output pipe creation failed. Error " +
                std::to_string(GetLastError()) + ".";
            return false;
        }

        readHandle.Reset(rawRead);
        writeHandle.Reset(rawWrite);

        if (SetHandleInformation(
                readHandle.Get(),
                HANDLE_FLAG_INHERIT,
                0
            ) == FALSE)
        {
            errorMessage =
                "Windows output pipe inheritance setup failed. Error " +
                std::to_string(GetLastError()) + ".";
            return false;
        }

        return true;
    }

    void ReadPipe(
        const HANDLE pipe,
        std::string& destination,
        const std::size_t maximumBytes,
        bool& truncated,
        std::string& readError,
        std::mutex& errorMutex
    )
    {
        std::array<char, 16U * 1024U> buffer{};

        for (;;)
        {
            DWORD bytesRead = 0;

            if (ReadFile(
                    pipe,
                    buffer.data(),
                    static_cast<DWORD>(buffer.size()),
                    &bytesRead,
                    nullptr
                ) == FALSE)
            {
                const DWORD error = GetLastError();

                if (error != ERROR_BROKEN_PIPE)
                {
                    const std::scoped_lock lock{errorMutex};

                    if (readError.empty())
                    {
                        readError =
                            "Windows process output read failed. Error " +
                            std::to_string(error) + ".";
                    }
                }

                break;
            }

            if (bytesRead == 0)
            {
                break;
            }

            const std::size_t remaining =
                destination.size() < maximumBytes
                    ? maximumBytes - destination.size()
                    : 0;

            const std::size_t accepted = std::min(
                remaining,
                static_cast<std::size_t>(bytesRead)
            );

            destination.append(buffer.data(), accepted);

            if (accepted < static_cast<std::size_t>(bytesRead))
            {
                truncated = true;
            }
        }
    }

    std::wstring BuildCommandLine(const ProcessRunOptions& options)
    {
        std::wstring commandLine =
            ProcessRunner::QuoteWindowsArgument(
                options.executablePath.wstring()
            );

        for (const std::wstring& argument : options.arguments)
        {
            commandLine.push_back(L' ');
            commandLine += ProcessRunner::QuoteWindowsArgument(argument);
        }

        return commandLine;
    }

    bool ConfigureKillOnCloseJob(
        const HANDLE job,
        std::string& errorMessage
    )
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

        if (SetInformationJobObject(
                job,
                JobObjectExtendedLimitInformation,
                &limits,
                sizeof(limits)
            ) == FALSE)
        {
            errorMessage =
                "Windows process job configuration failed. Error " +
                std::to_string(GetLastError()) + ".";
            return false;
        }

        return true;
    }
}

bool ProcessRunResult::Succeeded() const noexcept
{
    return reason == ProcessExitReason::Exited && exitCode == 0;
}

std::wstring ProcessRunner::QuoteWindowsArgument(
    const std::wstring_view argument
)
{
    if (argument.empty())
    {
        return L"\"\"";
    }

    const bool needsQuotes =
        argument.find_first_of(L" \t\n\v\"") != std::wstring_view::npos;

    if (!needsQuotes)
    {
        return std::wstring{argument};
    }

    std::wstring result;
    result.push_back(L'"');

    std::size_t backslashCount = 0;

    for (const wchar_t character : argument)
    {
        if (character == L'\\')
        {
            ++backslashCount;
            continue;
        }

        if (character == L'"')
        {
            result.append(backslashCount * 2U + 1U, L'\\');
            result.push_back(L'"');
            backslashCount = 0;
            continue;
        }

        result.append(backslashCount, L'\\');
        backslashCount = 0;
        result.push_back(character);
    }

    result.append(backslashCount * 2U, L'\\');
    result.push_back(L'"');

    return result;
}

ProcessRunResult ProcessRunner::Run(const ProcessRunOptions& options)
{
    ProcessRunResult result;

    if (options.executablePath.empty())
    {
        result.errorMessage = "The process executable path is empty.";
        return result;
    }

    UniqueHandle stdoutRead;
    UniqueHandle stdoutWrite;
    UniqueHandle stderrRead;
    UniqueHandle stderrWrite;

    if (!CreateCapturedPipe(
            stdoutRead,
            stdoutWrite,
            result.errorMessage
        ) ||
        !CreateCapturedPipe(
            stderrRead,
            stderrWrite,
            result.errorMessage
        ))
    {
        result.reason = ProcessExitReason::IoError;
        return result;
    }

    SECURITY_ATTRIBUTES nullInputSecurity{};
    nullInputSecurity.nLength = sizeof(nullInputSecurity);
    nullInputSecurity.bInheritHandle = TRUE;

    UniqueHandle nullInput{
        CreateFileW(
            L"NUL",
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &nullInputSecurity,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        )
    };

    if (!nullInput)
    {
        result.reason = ProcessExitReason::IoError;
        result.errorMessage =
            "Windows null-input handle creation failed. Error " +
            std::to_string(GetLastError()) + ".";
        return result;
    }

    UniqueHandle job{CreateJobObjectW(nullptr, nullptr)};

    if (!job)
    {
        result.reason = ProcessExitReason::IoError;
        result.errorMessage =
            "Windows process job creation failed. Error " +
            std::to_string(GetLastError()) + ".";
        return result;
    }

    if (!ConfigureKillOnCloseJob(job.Get(), result.errorMessage))
    {
        result.reason = ProcessExitReason::IoError;
        return result;
    }

    HANDLE inheritedHandles[]{
        stdoutWrite.Get(),
        stderrWrite.Get(),
        nullInput.Get()
    };

    AttributeList attributes;

    if (!attributes.Initialize(
            inheritedHandles,
            std::size(inheritedHandles),
            result.errorMessage
        ))
    {
        result.reason = ProcessExitReason::IoError;
        return result;
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = nullInput.Get();
    startup.StartupInfo.hStdOutput = stdoutWrite.Get();
    startup.StartupInfo.hStdError = stderrWrite.Get();
    startup.lpAttributeList = attributes.Get();

    PROCESS_INFORMATION processInformation{};

    std::wstring commandLine = BuildCommandLine(options);
    std::vector<wchar_t> mutableCommandLine(
        commandLine.begin(),
        commandLine.end()
    );
    mutableCommandLine.push_back(L'\0');

    const std::wstring executablePath =
        options.executablePath.wstring();

    const std::wstring workingDirectory =
        options.workingDirectory.wstring();

    const DWORD creationFlags =
        CREATE_NO_WINDOW |
        CREATE_SUSPENDED |
        EXTENDED_STARTUPINFO_PRESENT;

    if (CreateProcessW(
            executablePath.c_str(),
            mutableCommandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            creationFlags,
            nullptr,
            workingDirectory.empty()
                ? nullptr
                : workingDirectory.c_str(),
            &startup.StartupInfo,
            &processInformation
        ) == FALSE)
    {
        result.reason = ProcessExitReason::LaunchFailed;
        result.errorMessage =
            "Windows process creation failed. Error " +
            std::to_string(GetLastError()) + ".";
        return result;
    }

    UniqueHandle process{processInformation.hProcess};
    UniqueHandle primaryThread{processInformation.hThread};

    stdoutWrite.Reset();
    stderrWrite.Reset();
    nullInput.Reset();

    if (AssignProcessToJobObject(
            job.Get(),
            process.Get()
        ) == FALSE)
    {
        TerminateProcess(process.Get(), ERROR_PROCESS_ABORTED);
        WaitForSingleObject(process.Get(), 5000);

        result.reason = ProcessExitReason::LaunchFailed;
        result.errorMessage =
            "Windows process job assignment failed. Error " +
            std::to_string(GetLastError()) + ".";
        return result;
    }

    std::string pipeReadError;
    std::mutex pipeErrorMutex;

    std::thread stdoutThread{
        ReadPipe,
        stdoutRead.Get(),
        std::ref(result.standardOutput),
        options.maximumOutputBytes,
        std::ref(result.standardOutputTruncated),
        std::ref(pipeReadError),
        std::ref(pipeErrorMutex)
    };

    std::thread stderrThread{
        ReadPipe,
        stderrRead.Get(),
        std::ref(result.standardError),
        options.maximumOutputBytes,
        std::ref(result.standardErrorTruncated),
        std::ref(pipeReadError),
        std::ref(pipeErrorMutex)
    };

    const DWORD resumeResult = ResumeThread(primaryThread.Get());

    if (resumeResult == static_cast<DWORD>(-1))
    {
        TerminateJobObject(job.Get(), ERROR_PROCESS_ABORTED);
        WaitForSingleObject(process.Get(), 5000);

        result.reason = ProcessExitReason::LaunchFailed;
        result.errorMessage =
            "Windows process thread resume failed. Error " +
            std::to_string(GetLastError()) + ".";
    }
    else
    {
        const auto startedAt = std::chrono::steady_clock::now();

        for (;;)
        {
            const DWORD waitResult =
                WaitForSingleObject(process.Get(), 20);

            if (waitResult == WAIT_OBJECT_0)
            {
                result.reason = ProcessExitReason::Exited;
                break;
            }

            if (waitResult == WAIT_FAILED)
            {
                result.reason = ProcessExitReason::IoError;
                result.errorMessage =
                    "Windows process wait failed. Error " +
                    std::to_string(GetLastError()) + ".";
                TerminateJobObject(job.Get(), ERROR_PROCESS_ABORTED);
                WaitForSingleObject(process.Get(), 5000);
                break;
            }

            if (options.cancellationRequested != nullptr &&
                options.cancellationRequested->load())
            {
                result.reason = ProcessExitReason::Cancelled;
                result.errorMessage = "The process was cancelled.";
                TerminateJobObject(job.Get(), ERROR_CANCELLED);
                WaitForSingleObject(process.Get(), 5000);
                break;
            }

            if (options.timeout.count() > 0 &&
                std::chrono::steady_clock::now() - startedAt >=
                    options.timeout)
            {
                result.reason = ProcessExitReason::TimedOut;
                result.errorMessage = "The process timed out.";
                TerminateJobObject(job.Get(), WAIT_TIMEOUT);
                WaitForSingleObject(process.Get(), 5000);
                break;
            }
        }
    }

    DWORD exitCode = UINT32_MAX;

    if (GetExitCodeProcess(process.Get(), &exitCode) != FALSE)
    {
        result.exitCode = exitCode;
    }

    job.Reset();

    stdoutThread.join();
    stderrThread.join();

    if (result.reason == ProcessExitReason::Exited &&
        !pipeReadError.empty())
    {
        result.reason = ProcessExitReason::IoError;
        result.errorMessage = std::move(pipeReadError);
    }

    return result;
}
