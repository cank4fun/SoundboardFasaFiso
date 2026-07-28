#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <complex>
#include <span>

struct StereoCrosstalkCancellerSnapshot
{
    bool delayLocked = false;
    bool active = false;
    bool nearEndDetected = false;
    std::size_t delaySamples = 0;
    float delayConfidence = 0.0f;
    float blockCorrelation = 0.0f;
    float residualGain = 1.0f;
};

class StereoCrosstalkCanceller
{
public:
    static constexpr int ProcessingSampleRate = 48'000;
    static constexpr std::size_t SamplesPerBlock = 480;
    static constexpr std::size_t RenderChannelCount = 2;
    static constexpr std::size_t RenderSamplesPerBlock =
        SamplesPerBlock * RenderChannelCount;

    StereoCrosstalkCanceller();

    StereoCrosstalkCanceller(const StereoCrosstalkCanceller&) = delete;
    StereoCrosstalkCanceller& operator=(
        const StereoCrosstalkCanceller&
    ) = delete;

    StereoCrosstalkCanceller(StereoCrosstalkCanceller&&) = delete;
    StereoCrosstalkCanceller& operator=(
        StereoCrosstalkCanceller&&
    ) = delete;

    bool ProcessBlock(
        std::span<const float> renderReference,
        std::span<const float> microphoneInput,
        std::span<float> microphoneOutput
    ) noexcept;

    void ApplyResidualSuppression(
        std::span<float> microphoneOutput
    ) noexcept;

    [[nodiscard]] StereoCrosstalkCancellerSnapshot GetSnapshot()
        const noexcept;

    void Reset() noexcept;

private:
    static constexpr std::size_t MaximumDelaySamples = 4'800;
    static constexpr std::size_t FilterTapCount = 128;
    static constexpr std::size_t HistorySize = 16'384;
    static constexpr std::size_t HistoryMask = HistorySize - 1;
    static constexpr std::size_t DelayAnalysisWindowSamples = 8'192;
    static constexpr std::size_t DelayFftSize = 16'384;
    static constexpr std::size_t DelaySearchIntervalBlocks = 20;
    static constexpr std::size_t NearEndHoldBlocks = 8;

    static_assert((HistorySize & (HistorySize - 1)) == 0);
    static_assert(
        HistorySize > MaximumDelaySamples + FilterTapCount +
            SamplesPerBlock
    );

    struct CorrelationResult
    {
        std::size_t delaySamples = 0;
        float confidence = 0.0f;
    };

    [[nodiscard]] float RenderLeftAtOffset(
        std::size_t offset
    ) const noexcept;
    [[nodiscard]] float RenderRightAtOffset(
        std::size_t offset
    ) const noexcept;
    [[nodiscard]] float MicrophoneAtOffset(
        std::size_t offset
    ) const noexcept;

    [[nodiscard]] CorrelationResult EstimateDelay() noexcept;
    static void TransformFft(
        std::array<std::complex<float>, DelayFftSize>& samples,
        bool inverse
    ) noexcept;

    void UpdateDelayEstimate() noexcept;
    void ResetAdaptiveFilter() noexcept;
    void UpdateBlockState(
        std::span<const float> microphoneInput
    ) noexcept;

    std::array<float, HistorySize> renderLeftHistory_{};
    std::array<float, HistorySize> renderRightHistory_{};
    std::array<float, HistorySize> microphoneHistory_{};
    std::array<float, FilterTapCount> leftCoefficients_{};
    std::array<float, FilterTapCount> rightCoefficients_{};
    std::array<float, SamplesPerBlock> predictedEcho_{};
    std::array<float, DelayAnalysisWindowSamples> delayWindow_{};
    std::array<std::complex<float>, DelayFftSize> microphoneFft_{};
    std::array<std::complex<float>, DelayFftSize> workFft_{};
    std::array<std::complex<float>, DelayFftSize> crossFft_{};
    std::array<float, MaximumDelaySamples + 1> delayScores_{};

    std::size_t writeIndex_ = 0;
    std::uint64_t totalSamples_ = 0;
    std::uint64_t processedBlockCount_ = 0;
    std::uint64_t adaptedBlockCount_ = 0;
    std::uint64_t validatedModelBlockCount_ = 0;

    std::size_t delaySamples_ = 0;
    std::size_t pendingDelaySamples_ = 0;
    std::size_t pendingDelayObservationCount_ = 0;
    float delayConfidence_ = 0.0f;
    float blockCorrelation_ = 0.0f;
    float estimatedCoupling_ = 0.0f;
    float modelMix_ = 0.0f;
    float currentRenderRms_ = 0.0f;
    float currentMicrophoneRms_ = 0.0f;
    float currentExpectedEchoRms_ = 0.0f;
    float currentExcessRatio_ = 1.0f;
    bool currentRenderActive_ = false;
    float residualGain_ = 1.0f;
    float residualStartGain_ = 1.0f;
    float residualEndGain_ = 1.0f;

    std::size_t nearEndHoldRemaining_ = 0;
    std::size_t nearEndCandidateCount_ = 0;
    bool delayLocked_ = false;
    bool adaptationEnabled_ = false;
    bool active_ = false;
    bool nearEndDetected_ = false;
};
