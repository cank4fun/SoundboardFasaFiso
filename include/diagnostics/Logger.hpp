#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <streambuf>

class Logger
{
public:
    Logger();
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    bool Initialize(const std::filesystem::path& logsFolder);
    const std::filesystem::path& GetLogPath() const;

private:
    class TeeBuffer;

    void Shutdown();

    std::ofstream file_;
    std::mutex writeMutex_;
    std::unique_ptr<TeeBuffer> outputBuffer_;
    std::unique_ptr<TeeBuffer> errorBuffer_;
    std::streambuf* originalOutputBuffer_ = nullptr;
    std::streambuf* originalErrorBuffer_ = nullptr;
    std::filesystem::path logPath_;
};
