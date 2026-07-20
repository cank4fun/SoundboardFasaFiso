#include "platform/StartupManager.hpp"

#include <string>

namespace
{
    constexpr wchar_t RunKeyPath[] =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

    constexpr wchar_t ValueName[] =
        L"SoundBoardFasaFiso";
}

LSTATUS StartupManager::SetEnabled(
    const bool enabled,
    const std::filesystem::path& executablePath
)
{
    HKEY runKey = nullptr;

    const REGSAM access = enabled
        ? KEY_SET_VALUE
        : KEY_SET_VALUE | KEY_QUERY_VALUE;

    const LSTATUS openResult = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        RunKeyPath,
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        access,
        nullptr,
        &runKey,
        nullptr
    );

    if (openResult != ERROR_SUCCESS)
    {
        return openResult;
    }

    LSTATUS result = ERROR_SUCCESS;

    if (enabled)
    {
        const std::wstring command =
            L"\"" + executablePath.wstring() + L"\"";

        const DWORD byteCount = static_cast<DWORD>(
            (command.size() + 1) * sizeof(wchar_t)
        );

        result = RegSetValueExW(
            runKey,
            ValueName,
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            byteCount
        );
    }
    else
    {
        result = RegDeleteValueW(runKey, ValueName);

        if (result == ERROR_FILE_NOT_FOUND)
        {
            result = ERROR_SUCCESS;
        }
    }

    RegCloseKey(runKey);
    return result;
}
