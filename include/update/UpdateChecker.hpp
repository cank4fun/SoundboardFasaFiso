#pragma once

#include <string>
#include <string_view>

enum class UpdateCheckStatus
{
    UpdateAvailable,
    UpToDate,
    Failed
};

struct UpdateCheckResult
{
    UpdateCheckStatus status = UpdateCheckStatus::Failed;
    std::string latestVersion;
    std::string releaseUrl;
    std::string errorMessage;
};

namespace UpdateChecker
{
    UpdateCheckResult CheckLatestRelease(
        std::string_view currentVersion
    );
}
