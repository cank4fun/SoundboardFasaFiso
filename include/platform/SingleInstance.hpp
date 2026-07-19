#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <string_view>

enum class SingleInstanceResult
{
    Acquired,
    AlreadyRunning,
    Failed
};

class SingleInstance
{
public:
    SingleInstance() = default;
    ~SingleInstance();

    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    SingleInstance(SingleInstance&&) = delete;
    SingleInstance& operator=(SingleInstance&&) = delete;

    SingleInstanceResult Acquire(std::wstring_view mutexName);

private:
    HANDLE mutex_ = nullptr;
};
