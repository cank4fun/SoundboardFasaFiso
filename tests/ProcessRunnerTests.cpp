#include "platform/ProcessRunner.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

namespace
{
    bool Expect(const bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
        }

        return condition;
    }

    std::filesystem::path CurrentExecutablePath()
    {
        std::wstring buffer(32768, L'\0');

        const DWORD length = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size())
        );

        buffer.resize(length);
        return std::filesystem::path{buffer};
    }

    std::string NarrowAscii(const std::wstring& value)
    {
        std::string result;
        result.reserve(value.size());

        for (const wchar_t character : value)
        {
            result.push_back(
                character <= 0x7f
                    ? static_cast<char>(character)
                    : '?'
            );
        }

        return result;
    }

    int RunChildMode(const int argumentCount, wchar_t* arguments[])
    {
        if (argumentCount >= 2 &&
            std::wstring{arguments[1]} == L"--sleep")
        {
            std::this_thread::sleep_for(std::chrono::seconds{10});
            return 0;
        }

        if (argumentCount >= 2 &&
            std::wstring{arguments[1]} == L"--echo")
        {
            for (int index = 2; index < argumentCount; ++index)
            {
                std::cout
                    << "arg"
                    << index - 2
                    << '='
                    << NarrowAscii(arguments[index])
                    << '\n';
            }

            std::cerr << "stderr-marker\n";
            return 23;
        }

        return -1;
    }
}

int wmain(const int argumentCount, wchar_t* arguments[])
{
    const int childResult = RunChildMode(argumentCount, arguments);

    if (childResult >= 0)
    {
        return childResult;
    }

    if (!Expect(
            ProcessRunner::QuoteWindowsArgument(L"plain") == L"plain",
            "Simple argument quoting failed."
        ) ||
        !Expect(
            ProcessRunner::QuoteWindowsArgument(L"") == L"\"\"",
            "Empty argument quoting failed."
        ) ||
        !Expect(
            ProcessRunner::QuoteWindowsArgument(L"two words") ==
                L"\"two words\"",
            "Spaced argument quoting failed."
        ) ||
        !Expect(
            ProcessRunner::QuoteWindowsArgument(L"a\"b") ==
                L"\"a\\\"b\"",
            "Quoted argument escaping failed."
        ))
    {
        return 1;
    }

    ProcessRunOptions echoOptions;
    echoOptions.executablePath = CurrentExecutablePath();
    echoOptions.arguments = {
        L"--echo",
        L"plain",
        L"two words",
        L"quote\"inside",
        L"trailing slash \\"
    };
    echoOptions.timeout = std::chrono::seconds{5};

    const ProcessRunResult echoResult =
        ProcessRunner::Run(echoOptions);

    if (!Expect(
            echoResult.reason == ProcessExitReason::Exited,
            "Echo child did not exit normally."
        ) ||
        !Expect(
            echoResult.exitCode == 23,
            "Echo child exit code was not captured."
        ) ||
        !Expect(
            echoResult.standardOutput.find("arg0=plain") !=
                std::string::npos,
            "Plain argument was not captured."
        ) ||
        !Expect(
            echoResult.standardOutput.find("arg1=two words") !=
                std::string::npos,
            "Spaced argument was not preserved."
        ) ||
        !Expect(
            echoResult.standardOutput.find("arg2=quote\"inside") !=
                std::string::npos,
            "Quoted argument was not preserved."
        ) ||
        !Expect(
            echoResult.standardOutput.find("arg3=trailing slash \\") !=
                std::string::npos,
            "Trailing backslash argument was not preserved."
        ) ||
        !Expect(
            echoResult.standardError.find("stderr-marker") !=
                std::string::npos,
            "Standard error was not captured."
        ))
    {
        std::cerr << echoResult.errorMessage << '\n';
        return 1;
    }

    ProcessRunOptions timeoutOptions;
    timeoutOptions.executablePath = CurrentExecutablePath();
    timeoutOptions.arguments = {L"--sleep"};
    timeoutOptions.timeout = std::chrono::milliseconds{100};

    const ProcessRunResult timeoutResult =
        ProcessRunner::Run(timeoutOptions);

    if (!Expect(
            timeoutResult.reason == ProcessExitReason::TimedOut,
            "Timeout was not reported."
        ))
    {
        return 1;
    }

    std::atomic_bool cancellationRequested{false};

    std::thread cancellationThread{
        [&cancellationRequested]()
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds{100}
            );
            cancellationRequested.store(true);
        }
    };

    ProcessRunOptions cancellationOptions;
    cancellationOptions.executablePath = CurrentExecutablePath();
    cancellationOptions.arguments = {L"--sleep"};
    cancellationOptions.timeout = std::chrono::seconds{5};
    cancellationOptions.cancellationRequested =
        &cancellationRequested;

    const ProcessRunResult cancellationResult =
        ProcessRunner::Run(cancellationOptions);

    cancellationThread.join();

    if (!Expect(
            cancellationResult.reason ==
                ProcessExitReason::Cancelled,
            "Cancellation was not reported."
        ))
    {
        return 1;
    }

    ProcessRunOptions missingOptions;
    missingOptions.executablePath =
        CurrentExecutablePath().parent_path() /
        L"definitely-missing-process.exe";

    const ProcessRunResult missingResult =
        ProcessRunner::Run(missingOptions);

    if (!Expect(
            missingResult.reason ==
                ProcessExitReason::LaunchFailed,
            "Missing executable was not rejected."
        ))
    {
        return 1;
    }

    std::cout << "ProcessRunner tests passed.\n";
    return 0;
}
