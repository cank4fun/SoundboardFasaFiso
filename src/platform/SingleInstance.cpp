#include "platform/SingleInstance.hpp"

#include <string>

SingleInstance::~SingleInstance()
{
    if (mutex_ != nullptr)
    {
        CloseHandle(mutex_);
    }
}

SingleInstanceResult SingleInstance::Acquire(
    const std::wstring_view mutexName
)
{
    if (mutex_ != nullptr || mutexName.empty())
    {
        return SingleInstanceResult::Failed;
    }

    const std::wstring ownedName{mutexName};

    SetLastError(ERROR_SUCCESS);
    mutex_ = CreateMutexW(nullptr, FALSE, ownedName.c_str());

    if (mutex_ == nullptr)
    {
        return SingleInstanceResult::Failed;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(mutex_);
        mutex_ = nullptr;
        return SingleInstanceResult::AlreadyRunning;
    }

    return SingleInstanceResult::Acquired;
}
