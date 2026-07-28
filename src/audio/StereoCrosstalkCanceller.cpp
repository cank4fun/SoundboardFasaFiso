#include "audio/StereoCrosstalkCanceller.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace
{
    constexpr float MinimumRenderRms = 0.0008f;
    constexpr float MinimumMicrophoneRms = 0.00008f;
    constexpr float MinimumDelayConfidence = 0.16f;
    constexpr float MinimumAdaptationCorrelation = 0.18f;
    constexpr float StrongEchoCorrelation = 0.55f;
    constexpr float MinimumResidualGain = 0.008f;
    constexpr float MinimumNormalizationEnergy = 0.000001f;
    constexpr float InitialNlmsStep = 0.16f;
    constexpr float TrackingNlmsStep = 0.045f;
    constexpr float CoefficientLimit = 2.0f;
    constexpr float CouplingSmoothing = 0.08f;
    constexpr float MaximumObservedCoupling = 1.5f;
    constexpr float ModelRampBlocks = 30.0f;

    float SanitizedSample(const float value) noexcept
    {
        return std::isfinite(value)
            ? std::clamp(value, -1.0f, 1.0f)
            : 0.0f;
    }

    float RootMeanSquare(
        const std::span<const float> samples
    ) noexcept
    {
        double squareSum = 0.0;
        for (const float sample : samples)
        {
            squareSum += static_cast<double>(sample) *
                static_cast<double>(sample);
        }

        if (samples.empty())
        {
            return 0.0f;
        }

        return static_cast<float>(std::sqrt(
            squareSum / static_cast<double>(samples.size())
        ));
    }

    float SaturatingRange(
        const float value,
        const float inactiveAt,
        const float activeAt
    ) noexcept
    {
        if (value <= inactiveAt)
        {
            return 0.0f;
        }
        if (value >= activeAt)
        {
            return 1.0f;
        }
        return (value - inactiveAt) / (activeAt - inactiveAt);
    }

    float LinearFromDecibels(const float decibels) noexcept
    {
        return std::pow(10.0f, decibels / 20.0f);
    }
}

StereoCrosstalkCanceller::StereoCrosstalkCanceller()
{
    constexpr float denominator = static_cast<float>(
        DelayAnalysisWindowSamples - 1
    );
    for (std::size_t index = 0;
        index < DelayAnalysisWindowSamples;
        ++index)
    {
        delayWindow_[index] = 0.5f - 0.5f * std::cos(
            2.0f * std::numbers::pi_v<float> *
            static_cast<float>(index) / denominator
        );
    }
}

bool StereoCrosstalkCanceller::ProcessBlock(
    const std::span<const float> renderReference,
    const std::span<const float> microphoneInput,
    const std::span<float> microphoneOutput
) noexcept
{
    if (renderReference.size() != RenderSamplesPerBlock ||
        microphoneInput.size() != SamplesPerBlock ||
        microphoneOutput.size() != SamplesPerBlock)
    {
        if (microphoneInput.size() == microphoneOutput.size())
        {
            std::copy(
                microphoneInput.begin(),
                microphoneInput.end(),
                microphoneOutput.begin()
            );
        }
        return false;
    }

    const float nlmsStep = adaptedBlockCount_ < 80
        ? InitialNlmsStep
        : TrackingNlmsStep;

    for (std::size_t frame = 0; frame < SamplesPerBlock; ++frame)
    {
        const std::size_t renderIndex = frame * RenderChannelCount;
        const float left = SanitizedSample(renderReference[renderIndex]);
        const float right = SanitizedSample(
            renderReference[renderIndex + 1]
        );
        const float microphone = SanitizedSample(microphoneInput[frame]);

        renderLeftHistory_[writeIndex_] = left;
        renderRightHistory_[writeIndex_] = right;
        microphoneHistory_[writeIndex_] = microphone;
        writeIndex_ = (writeIndex_ + 1) & HistoryMask;
        ++totalSamples_;

        float predicted = 0.0f;
        float normalizationEnergy = MinimumNormalizationEnergy;

        if (delayLocked_ &&
            totalSamples_ >= delaySamples_ + FilterTapCount)
        {
            for (std::size_t tap = 0; tap < FilterTapCount; ++tap)
            {
                const std::size_t offset = delaySamples_ + tap;
                const float delayedLeft = RenderLeftAtOffset(offset);
                const float delayedRight = RenderRightAtOffset(offset);

                predicted += leftCoefficients_[tap] * delayedLeft +
                    rightCoefficients_[tap] * delayedRight;
                normalizationEnergy += delayedLeft * delayedLeft +
                    delayedRight * delayedRight;
            }
        }

        predictedEcho_[frame] = predicted;
        const float error = microphone - predicted;
        microphoneOutput[frame] = microphone - modelMix_ * predicted;

        if (adaptationEnabled_ && delayLocked_ &&
            normalizationEnergy > MinimumNormalizationEnergy)
        {
            const float normalizedCorrection = std::clamp(
                nlmsStep * error / normalizationEnergy,
                -0.25f,
                0.25f
            );

            for (std::size_t tap = 0; tap < FilterTapCount; ++tap)
            {
                const std::size_t offset = delaySamples_ + tap;
                const float delayedLeft = RenderLeftAtOffset(offset);
                const float delayedRight = RenderRightAtOffset(offset);

                leftCoefficients_[tap] = std::clamp(
                    leftCoefficients_[tap] +
                        normalizedCorrection * delayedLeft,
                    -CoefficientLimit,
                    CoefficientLimit
                );
                rightCoefficients_[tap] = std::clamp(
                    rightCoefficients_[tap] +
                        normalizedCorrection * delayedRight,
                    -CoefficientLimit,
                    CoefficientLimit
                );
            }
        }

    }

    ++processedBlockCount_;

    if (processedBlockCount_ % DelaySearchIntervalBlocks == 0)
    {
        UpdateDelayEstimate();
    }

    UpdateBlockState(microphoneInput);
    return true;
}

void StereoCrosstalkCanceller::ApplyResidualSuppression(
    const std::span<float> microphoneOutput
) noexcept
{
    if (microphoneOutput.size() != SamplesPerBlock)
    {
        return;
    }

    if (!delayLocked_)
    {
        nearEndHoldRemaining_ = 0;
        nearEndCandidateCount_ = 0;
        nearEndDetected_ = false;
        residualGain_ = 1.0f;
        residualStartGain_ = 1.0f;
        residualEndGain_ = 1.0f;
        active_ = false;
        return;
    }

    const float outputRms = RootMeanSquare(microphoneOutput);
    const float outputToInputRatio = outputRms /
        std::max(currentMicrophoneRms_, MinimumMicrophoneRms);

    const bool immediateNearEnd = outputRms > 0.025f &&
        (currentExcessRatio_ > 1.25f || blockCorrelation_ < 0.70f);
    const bool nearEndCandidate = currentRenderActive_
        ? outputRms > 0.0070f &&
            outputToInputRatio > 0.22f &&
            ((currentExcessRatio_ > 1.45f &&
                blockCorrelation_ < 0.65f) ||
                blockCorrelation_ < 0.45f)
        : outputRms > 0.0012f;

    if (nearEndCandidate)
    {
        nearEndCandidateCount_ = std::min<std::size_t>(
            nearEndCandidateCount_ + 1,
            3
        );
    }
    else
    {
        nearEndCandidateCount_ = 0;
    }

    const bool nearEndNow = immediateNearEnd ||
        nearEndCandidateCount_ >= 2;

    if (nearEndNow)
    {
        nearEndHoldRemaining_ = NearEndHoldBlocks;
    }
    else if (nearEndHoldRemaining_ > 0)
    {
        --nearEndHoldRemaining_;
    }
    nearEndDetected_ = nearEndNow || nearEndHoldRemaining_ > 0;

    const float renderConfidence = SaturatingRange(
        currentRenderRms_,
        MinimumRenderRms,
        0.025f
    );
    const float correlationConfidence = SaturatingRange(
        blockCorrelation_,
        MinimumAdaptationCorrelation,
        StrongEchoCorrelation
    );
    const float delayLockConfidence = delayLocked_
        ? SaturatingRange(
            delayConfidence_,
            MinimumDelayConfidence,
            0.55f
        )
        : 0.0f;
    const float aecReductionConfidence = 1.0f - SaturatingRange(
        outputToInputRatio,
        0.12f,
        0.75f
    );

    const float echoEvidence = std::max(
        correlationConfidence,
        0.90f * aecReductionConfidence
    );
    float echoOnlyConfidence = renderConfidence *
        std::max(delayLockConfidence, echoEvidence) *
        echoEvidence;

    if (nearEndDetected_ || !currentRenderActive_)
    {
        echoOnlyConfidence = 0.0f;
    }

    echoOnlyConfidence = std::clamp(echoOnlyConfidence, 0.0f, 1.0f);
    const float attenuationDb = -48.0f *
        std::pow(echoOnlyConfidence, 1.20f);
    const float targetGain = std::clamp(
        LinearFromDecibels(attenuationDb),
        MinimumResidualGain,
        1.0f
    );

    residualStartGain_ = residualGain_;
    if (nearEndDetected_)
    {
        residualStartGain_ = 1.0f;
        residualGain_ = 1.0f;
    }
    else
    {
        const float response = targetGain < residualGain_
            ? 0.90f
            : 0.22f;
        residualGain_ += response * (targetGain - residualGain_);
        residualGain_ = std::clamp(
            residualGain_,
            MinimumResidualGain,
            1.0f
        );
    }
    residualEndGain_ = residualGain_;
    active_ = !nearEndDetected_ && currentRenderActive_ &&
        residualGain_ < 0.98f;

    const float startGain = residualStartGain_;
    const float endGain = residualEndGain_;
    for (std::size_t frame = 0; frame < SamplesPerBlock; ++frame)
    {
        const float progress = static_cast<float>(frame + 1) /
            static_cast<float>(SamplesPerBlock);
        const float gain = std::lerp(startGain, endGain, progress);
        microphoneOutput[frame] *= gain;
    }
}

StereoCrosstalkCancellerSnapshot
StereoCrosstalkCanceller::GetSnapshot() const noexcept
{
    StereoCrosstalkCancellerSnapshot snapshot;
    snapshot.delayLocked = delayLocked_;
    snapshot.active = active_;
    snapshot.nearEndDetected = nearEndDetected_;
    snapshot.delaySamples = delaySamples_;
    snapshot.delayConfidence = delayConfidence_;
    snapshot.blockCorrelation = blockCorrelation_;
    snapshot.residualGain = residualGain_;
    return snapshot;
}

void StereoCrosstalkCanceller::Reset() noexcept
{
    renderLeftHistory_.fill(0.0f);
    renderRightHistory_.fill(0.0f);
    microphoneHistory_.fill(0.0f);
    ResetAdaptiveFilter();

    writeIndex_ = 0;
    totalSamples_ = 0;
    processedBlockCount_ = 0;
    delaySamples_ = 0;
    pendingDelaySamples_ = 0;
    pendingDelayObservationCount_ = 0;
    delayConfidence_ = 0.0f;
    blockCorrelation_ = 0.0f;
    estimatedCoupling_ = 0.0f;
    currentRenderRms_ = 0.0f;
    currentMicrophoneRms_ = 0.0f;
    currentExpectedEchoRms_ = 0.0f;
    currentExcessRatio_ = 1.0f;
    currentRenderActive_ = false;
    residualGain_ = 1.0f;
    residualStartGain_ = 1.0f;
    residualEndGain_ = 1.0f;
    nearEndHoldRemaining_ = 0;
    nearEndCandidateCount_ = 0;
    delayLocked_ = false;
    adaptationEnabled_ = false;
    active_ = false;
    nearEndDetected_ = false;
}

float StereoCrosstalkCanceller::RenderLeftAtOffset(
    const std::size_t offset
) const noexcept
{
    const std::size_t newest = (writeIndex_ - 1) & HistoryMask;
    return renderLeftHistory_[(newest - offset) & HistoryMask];
}

float StereoCrosstalkCanceller::RenderRightAtOffset(
    const std::size_t offset
) const noexcept
{
    const std::size_t newest = (writeIndex_ - 1) & HistoryMask;
    return renderRightHistory_[(newest - offset) & HistoryMask];
}

float StereoCrosstalkCanceller::MicrophoneAtOffset(
    const std::size_t offset
) const noexcept
{
    const std::size_t newest = (writeIndex_ - 1) & HistoryMask;
    return microphoneHistory_[(newest - offset) & HistoryMask];
}

StereoCrosstalkCanceller::CorrelationResult
StereoCrosstalkCanceller::EstimateDelay() noexcept
{
    CorrelationResult result;

    if (totalSamples_ < DelayAnalysisWindowSamples + MaximumDelaySamples)
    {
        return result;
    }

    microphoneFft_.fill({0.0f, 0.0f});
    delayScores_.fill(0.0f);

    for (std::size_t index = 0;
        index < DelayAnalysisWindowSamples;
        ++index)
    {
        const std::size_t offset =
            DelayAnalysisWindowSamples - 1 - index;
        microphoneFft_[index] = {
            MicrophoneAtOffset(offset) * delayWindow_[index],
            0.0f
        };
    }
    TransformFft(microphoneFft_, false);

    for (std::size_t channel = 0;
        channel < RenderChannelCount;
        ++channel)
    {
        workFft_.fill({0.0f, 0.0f});

        for (std::size_t index = 0;
            index < DelayAnalysisWindowSamples;
            ++index)
        {
            const std::size_t offset =
                DelayAnalysisWindowSamples - 1 - index;
            const float render = channel == 0
                ? RenderLeftAtOffset(offset)
                : RenderRightAtOffset(offset);
            workFft_[index] = {
                render * delayWindow_[index],
                0.0f
            };
        }
        TransformFft(workFft_, false);

        for (std::size_t bin = 0; bin < DelayFftSize; ++bin)
        {
            const std::complex<float> cross =
                microphoneFft_[bin] * std::conj(workFft_[bin]);
            const float magnitude = std::abs(cross);
            crossFft_[bin] = magnitude > 0.00000001f
                ? cross / magnitude
                : std::complex<float>{0.0f, 0.0f};
        }
        TransformFft(crossFft_, true);

        for (std::size_t delay = 0;
            delay <= MaximumDelaySamples;
            ++delay)
        {
            delayScores_[delay] += std::abs(
                crossFft_[delay].real()
            );
        }
    }

    float peakScore = 0.0f;
    for (std::size_t delay = 0;
        delay <= MaximumDelaySamples;
        ++delay)
    {
        if (delayScores_[delay] > peakScore)
        {
            peakScore = delayScores_[delay];
            result.delaySamples = delay;
        }
    }

    float secondScore = 0.0f;
    constexpr std::size_t ExclusionRadius = 24;
    for (std::size_t delay = 0;
        delay <= MaximumDelaySamples;
        ++delay)
    {
        const std::size_t difference = delay > result.delaySamples
            ? delay - result.delaySamples
            : result.delaySamples - delay;
        if (difference > ExclusionRadius)
        {
            secondScore = std::max(secondScore, delayScores_[delay]);
        }
    }

    const float peakRatio = peakScore /
        std::max(secondScore, 0.000001f);
    result.confidence = std::clamp(
        (peakRatio - 1.0f) / 2.0f,
        0.0f,
        1.0f
    );
    return result;
}

void StereoCrosstalkCanceller::TransformFft(
    std::array<std::complex<float>, DelayFftSize>& samples,
    const bool inverse
) noexcept
{
    for (std::size_t index = 1, reversed = 0;
        index < DelayFftSize;
        ++index)
    {
        std::size_t bit = DelayFftSize >> 1U;
        for (; (reversed & bit) != 0; bit >>= 1U)
        {
            reversed ^= bit;
        }
        reversed ^= bit;
        if (index < reversed)
        {
            std::swap(samples[index], samples[reversed]);
        }
    }

    for (std::size_t length = 2;
        length <= DelayFftSize;
        length <<= 1U)
    {
        const float angle = (inverse ? 2.0f : -2.0f) *
            std::numbers::pi_v<float> /
            static_cast<float>(length);
        const std::complex<float> step{
            std::cos(angle),
            std::sin(angle)
        };

        for (std::size_t start = 0;
            start < DelayFftSize;
            start += length)
        {
            std::complex<float> phase{1.0f, 0.0f};
            const std::size_t halfLength = length >> 1U;
            for (std::size_t offset = 0;
                offset < halfLength;
                ++offset)
            {
                const std::complex<float> even = samples[start + offset];
                const std::complex<float> odd =
                    samples[start + offset + halfLength] * phase;
                samples[start + offset] = even + odd;
                samples[start + offset + halfLength] = even - odd;
                phase *= step;
            }
        }
    }

    if (inverse)
    {
        const float inverseSize = 1.0f /
            static_cast<float>(DelayFftSize);
        for (std::complex<float>& sample : samples)
        {
            sample *= inverseSize;
        }
    }
}

void StereoCrosstalkCanceller::UpdateDelayEstimate() noexcept
{
    const CorrelationResult estimate = EstimateDelay();

    if (estimate.confidence < MinimumDelayConfidence)
    {
        delayConfidence_ *= 0.9f;
        return;
    }

    delayConfidence_ = estimate.confidence;

    if (!delayLocked_)
    {
        if (estimate.confidence >= 0.55f)
        {
            delaySamples_ = estimate.delaySamples;
            delayLocked_ = true;
            nearEndHoldRemaining_ = 0;
            nearEndCandidateCount_ = 0;
            nearEndDetected_ = false;
            ResetAdaptiveFilter();
            return;
        }

        const std::size_t difference = estimate.delaySamples >
            pendingDelaySamples_
            ? estimate.delaySamples - pendingDelaySamples_
            : pendingDelaySamples_ - estimate.delaySamples;

        if (pendingDelayObservationCount_ == 0 || difference > 32)
        {
            pendingDelaySamples_ = estimate.delaySamples;
            pendingDelayObservationCount_ = 1;
            return;
        }

        pendingDelaySamples_ = (
            pendingDelaySamples_ * pendingDelayObservationCount_ +
            estimate.delaySamples
        ) / (pendingDelayObservationCount_ + 1);
        ++pendingDelayObservationCount_;

        if (pendingDelayObservationCount_ >= 2)
        {
            delaySamples_ = pendingDelaySamples_;
            delayLocked_ = true;
            nearEndHoldRemaining_ = 0;
            nearEndCandidateCount_ = 0;
            nearEndDetected_ = false;
            ResetAdaptiveFilter();
        }
        return;
    }

    const std::size_t difference = estimate.delaySamples > delaySamples_
        ? estimate.delaySamples - delaySamples_
        : delaySamples_ - estimate.delaySamples;

    if (difference <= 48)
    {
        delaySamples_ = (delaySamples_ * 3 + estimate.delaySamples) / 4;
    }
    else if (estimate.confidence > 0.68f)
    {
        delaySamples_ = estimate.delaySamples;
        ResetAdaptiveFilter();
    }
}

void StereoCrosstalkCanceller::ResetAdaptiveFilter() noexcept
{
    leftCoefficients_.fill(0.0f);
    rightCoefficients_.fill(0.0f);
    predictedEcho_.fill(0.0f);
    adaptedBlockCount_ = 0;
    validatedModelBlockCount_ = 0;
    modelMix_ = 0.0f;
    adaptationEnabled_ = false;
}

void StereoCrosstalkCanceller::UpdateBlockState(
    const std::span<const float> microphoneInput
) noexcept
{
    double renderPower = 0.0;
    double microphonePower = 0.0;
    double predictedPower = 0.0;
    double candidateErrorPower = 0.0;
    double leftPower = 0.0;
    double rightPower = 0.0;
    double midPower = 0.0;
    double microphoneLeft = 0.0;
    double microphoneRight = 0.0;
    double microphoneMid = 0.0;

    if (delayLocked_ && totalSamples_ >= delaySamples_ + SamplesPerBlock + 2)
    {
        for (std::size_t frame = 0; frame < SamplesPerBlock; ++frame)
        {
            const std::size_t newestOffset =
                SamplesPerBlock - 1 - frame;
            const float microphone = SanitizedSample(
                microphoneInput[frame]
            );
            const float left = RenderLeftAtOffset(
                newestOffset + delaySamples_
            );
            const float right = RenderRightAtOffset(
                newestOffset + delaySamples_
            );
            const float mid = 0.5f * (left + right);
            const float predicted = predictedEcho_[frame];
            const float candidateError = microphone - predicted;

            renderPower += 0.5 * (
                static_cast<double>(left) * left +
                static_cast<double>(right) * right
            );
            microphonePower += static_cast<double>(microphone) * microphone;
            predictedPower += static_cast<double>(predicted) * predicted;
            candidateErrorPower +=
                static_cast<double>(candidateError) * candidateError;
            leftPower += static_cast<double>(left) * left;
            rightPower += static_cast<double>(right) * right;
            midPower += static_cast<double>(mid) * mid;
            microphoneLeft += static_cast<double>(microphone) * left;
            microphoneRight += static_cast<double>(microphone) * right;
            microphoneMid += static_cast<double>(microphone) * mid;
        }
    }
    else
    {
        const float microphoneRms = RootMeanSquare(microphoneInput);
        microphonePower = static_cast<double>(microphoneRms) *
            microphoneRms * SamplesPerBlock;
        candidateErrorPower = microphonePower;
    }

    const float inverseBlock = 1.0f /
        static_cast<float>(SamplesPerBlock);
    currentRenderRms_ = static_cast<float>(std::sqrt(
        renderPower * inverseBlock
    ));
    currentMicrophoneRms_ = static_cast<float>(std::sqrt(
        microphonePower * inverseBlock
    ));
    const float predictedRms = static_cast<float>(std::sqrt(
        predictedPower * inverseBlock
    ));
    const float candidateErrorRms = static_cast<float>(std::sqrt(
        candidateErrorPower * inverseBlock
    ));

    const auto normalized = [microphonePower](
        const double cross,
        const double referencePower
    ) noexcept
    {
        if (microphonePower <= 1.0e-12 || referencePower <= 1.0e-12)
        {
            return 0.0f;
        }
        return static_cast<float>(std::abs(cross) /
            std::sqrt(microphonePower * referencePower));
    };

    blockCorrelation_ = delayLocked_
        ? std::clamp(
            std::max({
                normalized(microphoneLeft, leftPower),
                normalized(microphoneRight, rightPower),
                normalized(microphoneMid, midPower)
            }),
            0.0f,
            1.0f
        )
        : 0.0f;

    currentRenderActive_ = currentRenderRms_ >= MinimumRenderRms;
    const float observedCoupling = currentRenderActive_
        ? currentMicrophoneRms_ /
            std::max(currentRenderRms_, MinimumRenderRms)
        : 0.0f;

    if (delayLocked_ && currentRenderActive_ &&
        blockCorrelation_ >= 0.32f &&
        observedCoupling <= MaximumObservedCoupling)
    {
        if (estimatedCoupling_ <= 0.0f)
        {
            estimatedCoupling_ = observedCoupling;
        }
        else
        {
            estimatedCoupling_ = std::lerp(
                estimatedCoupling_,
                observedCoupling,
                CouplingSmoothing
            );
        }
    }

    currentExpectedEchoRms_ = std::max(
        predictedRms * modelMix_,
        estimatedCoupling_ * currentRenderRms_
    );
    currentExcessRatio_ = currentMicrophoneRms_ /
        std::max(currentExpectedEchoRms_, MinimumMicrophoneRms);

    const bool echoDominantForAdaptation = delayLocked_ &&
        currentRenderActive_ &&
        !nearEndDetected_ &&
        blockCorrelation_ >= MinimumAdaptationCorrelation &&
        currentExcessRatio_ < 2.6f;

    adaptationEnabled_ = echoDominantForAdaptation;
    if (adaptationEnabled_)
    {
        ++adaptedBlockCount_;
    }

    const bool modelImprovesSignal =
        currentMicrophoneRms_ > MinimumMicrophoneRms &&
        candidateErrorRms < currentMicrophoneRms_ * 0.92f;
    if (echoDominantForAdaptation && modelImprovesSignal)
    {
        validatedModelBlockCount_ = std::min<std::uint64_t>(
            validatedModelBlockCount_ + 1,
            60
        );
    }
    else if (validatedModelBlockCount_ > 0)
    {
        --validatedModelBlockCount_;
    }

    modelMix_ = std::clamp(
        static_cast<float>(validatedModelBlockCount_) / ModelRampBlocks,
        0.0f,
        1.0f
    );
}
