#pragma once

#include <cstddef>
#include <cstdint>

enum class VoiceEngineSelfTestCheck : std::uint32_t
{
    ProcessorContract = 1U << 0U,
    TransparentDisabledPath = 1U << 1U,
    ActiveSignalPath = 1U << 2U,
    FiniteBoundedOutput = 1U << 3U,
    DynamicBypass = 1U << 4U,
    RackOrderResponse = 1U << 5U,
    ResetAndReinitialize = 1U << 6U
};

struct VoiceEngineSelfTestReport
{
    std::uint32_t completedMask = 0;
    std::uint32_t failedMask = 0;
    std::uint32_t checkCount = 0;
    std::uint32_t failureCount = 0;
    std::uint32_t processedBlockCount = 0;
    std::size_t latencySamples = 0;
    float inputRms = 0.0f;
    float outputRms = 0.0f;
    float outputPeak = 0.0f;
    float maximumSampleStep = 0.0f;
    float disabledPathMaximumError = 0.0f;
    float bypassMaximumError = 0.0f;
    float rackDifferenceRms = 0.0f;

    [[nodiscard]] bool Passed() const noexcept
    {
        return checkCount != 0 && failureCount == 0;
    }
};

[[nodiscard]] VoiceEngineSelfTestReport RunVoiceEngineSelfTest() noexcept;
