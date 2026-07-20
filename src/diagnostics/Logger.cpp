#include "diagnostics/Logger.hpp"

#include "platform/DebugConsole.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <string>
#include <system_error>

class Logger::TeeBuffer final : public std::streambuf
{
public:
    TeeBuffer(
        std::streambuf* fileBuffer,
        const bool errorStream,
        std::mutex& writeMutex
    )
        : fileBuffer_(fileBuffer),
          errorStream_(errorStream),
          writeMutex_(writeMutex)
    {
    }

protected:
    int_type overflow(const int_type character) override
    {
        if (traits_type::eq_int_type(character, traits_type::eof()))
        {
            return traits_type::not_eof(character);
        }

        const char value = traits_type::to_char_type(character);
        std::lock_guard lock(writeMutex_);

        const bool fileSucceeded =
            fileBuffer_ == nullptr ||
            !traits_type::eq_int_type(
                fileBuffer_->sputc(value),
                traits_type::eof()
            );

        DebugConsole::Write(&value, 1, errorStream_);

        return fileSucceeded
            ? character
            : traits_type::eof();
    }

    std::streamsize xsputn(
        const char* text,
        const std::streamsize count
    ) override
    {
        std::lock_guard lock(writeMutex_);

        const std::streamsize fileWritten =
            fileBuffer_ == nullptr
                ? count
                : fileBuffer_->sputn(text, count);

        DebugConsole::Write(
            text,
            count > 0 ? static_cast<std::size_t>(count) : 0,
            errorStream_
        );

        return fileWritten;
    }

    int sync() override
    {
        std::lock_guard lock(writeMutex_);
        return fileBuffer_ == nullptr || fileBuffer_->pubsync() == 0
            ? 0
            : -1;
    }

private:
    std::streambuf* fileBuffer_ = nullptr;
    bool errorStream_ = false;
    std::mutex& writeMutex_;
};

Logger::Logger() = default;

Logger::~Logger()
{
    Shutdown();
}

bool Logger::Initialize(const std::filesystem::path& logsFolder)
{
    Shutdown();

    if (logsFolder.empty())
    {
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(logsFolder, error);

    if (error)
    {
        return false;
    }

    logPath_ = logsFolder / L"latest.log";
    const std::filesystem::path previousLogPath =
        logsFolder / L"previous.log";

    error.clear();
    std::filesystem::remove(previousLogPath, error);
    error.clear();

    if (std::filesystem::exists(logPath_, error) && !error)
    {
        error.clear();
        std::filesystem::rename(logPath_, previousLogPath, error);
    }

    error.clear();
    file_.open(
        logPath_,
        std::ios::binary | std::ios::out | std::ios::trunc
    );

    if (!file_.is_open())
    {
        logPath_.clear();
        return false;
    }

    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_s(&localTime, &time);

    file_
        << "SoundBoardFasaFiso session log\n"
        << "Started: "
        << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S")
        << "\n\n";
    file_.flush();

    originalOutputBuffer_ = std::cout.rdbuf();
    originalErrorBuffer_ = std::cerr.rdbuf();

    outputBuffer_ = std::make_unique<TeeBuffer>(
        file_.rdbuf(),
        false,
        writeMutex_
    );
    errorBuffer_ = std::make_unique<TeeBuffer>(
        file_.rdbuf(),
        true,
        writeMutex_
    );

    std::cout.rdbuf(outputBuffer_.get());
    std::cerr.rdbuf(errorBuffer_.get());
    std::cout.clear();
    std::cerr.clear();

    return true;
}

const std::filesystem::path& Logger::GetLogPath() const
{
    return logPath_;
}

void Logger::Shutdown()
{
    if (originalOutputBuffer_ != nullptr)
    {
        std::cout.flush();
        std::cout.rdbuf(originalOutputBuffer_);
    }

    if (originalErrorBuffer_ != nullptr)
    {
        std::cerr.flush();
        std::cerr.rdbuf(originalErrorBuffer_);
    }

    outputBuffer_.reset();
    errorBuffer_.reset();
    originalOutputBuffer_ = nullptr;
    originalErrorBuffer_ = nullptr;

    if (file_.is_open())
    {
        file_.flush();
        file_.close();
    }
}
