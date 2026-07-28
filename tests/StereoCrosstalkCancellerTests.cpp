#include "audio/StereoCrosstalkCanceller.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace
{
    constexpr float TwoPi = 6.28318530717958647692f;
    constexpr std::size_t SyntheticDelaySamples = 963;

    struct NoiseGenerator
    {
        std::uint32_t state = 0x12345678U;

        float Next() noexcept
        {
            state ^= state << 13U;
            state ^= state >> 17U;
            state ^= state << 5U;
            const float normalized = static_cast<float>(state) /
                static_cast<float>(UINT32_MAX);
            return normalized * 2.0f - 1.0f;
        }
    };

    struct SyntheticStream
    {
        NoiseGenerator noise;
        std::vector<float> left;
        std::vector<float> right;
        float leftState = 0.0f;
        float rightState = 0.0f;
        std::size_t sampleIndex = 0;

        SyntheticStream()
        {
            left.reserve(400'000);
            right.reserve(400'000);
        }

        void FillBlock(
            std::array<float,
                StereoCrosstalkCanceller::RenderSamplesPerBlock>& render,
            std::array<float,
                StereoCrosstalkCanceller::SamplesPerBlock>& microphone,
            const bool includeNearEnd
        )
        {
            for (std::size_t frame = 0;
                frame < StereoCrosstalkCanceller::SamplesPerBlock;
                ++frame)
            {
                leftState = 0.72f * leftState + 0.20f * noise.Next();
                rightState = 0.65f * rightState + 0.22f * noise.Next();
                const float leftSample = std::clamp(
                    leftState,
                    -0.8f,
                    0.8f
                );
                const float rightSample = std::clamp(
                    rightState,
                    -0.8f,
                    0.8f
                );
                left.push_back(leftSample);
                right.push_back(rightSample);

                const std::size_t renderIndex = frame * 2;
                render[renderIndex] = leftSample;
                render[renderIndex + 1] = rightSample;

                float leak = 0.0f;
                if (sampleIndex >= SyntheticDelaySamples + 18)
                {
                    const std::size_t base =
                        sampleIndex - SyntheticDelaySamples;
                    leak = 0.13f * left[base] +
                        0.065f * right[base] +
                        0.035f * left[base - 11] -
                        0.022f * right[base - 18];
                }

                float nearEnd = 0.0f;
                if (includeNearEnd)
                {
                    nearEnd = 0.22f * std::sin(
                        TwoPi * 310.0f *
                        static_cast<float>(sampleIndex) /
                        static_cast<float>(
                            StereoCrosstalkCanceller::ProcessingSampleRate
                        )
                    );
                }

                microphone[frame] = leak + nearEnd;
                ++sampleIndex;
            }
        }
    };

    double SquareSum(const std::span<const float> samples)
    {
        double result = 0.0;
        for (const float sample : samples)
        {
            result += static_cast<double>(sample) * sample;
        }
        return result;
    }

    bool TestInvalidShapeSafelyBypasses()
    {
        StereoCrosstalkCanceller canceller;
        std::array<float, 4> render{0.1f, -0.1f, 0.2f, -0.2f};
        std::array<float, 4> microphone{0.3f, -0.3f, 0.4f, -0.4f};
        std::array<float, 4> output{};

        const bool processed = canceller.ProcessBlock(
            render,
            microphone,
            output
        );

        return !processed && output == microphone;
    }

    bool TestAutomaticDelayAndLinearCancellation()
    {
        StereoCrosstalkCanceller canceller;
        SyntheticStream stream;
        std::array<float,
            StereoCrosstalkCanceller::RenderSamplesPerBlock> render{};
        std::array<float,
            StereoCrosstalkCanceller::SamplesPerBlock> microphone{};
        std::array<float,
            StereoCrosstalkCanceller::SamplesPerBlock> cleaned{};
        std::array<float,
            StereoCrosstalkCanceller::SamplesPerBlock> residual{};

        double inputEnergy = 0.0;
        double cleanedEnergy = 0.0;
        double residualEnergy = 0.0;
        constexpr std::size_t BlockCount = 320;
        constexpr std::size_t MeasurementStartBlock = 240;

        for (std::size_t block = 0; block < BlockCount; ++block)
        {
            stream.FillBlock(render, microphone, false);
            if (!canceller.ProcessBlock(render, microphone, cleaned))
            {
                return false;
            }
            residual = cleaned;
            canceller.ApplyResidualSuppression(residual);

            if (block >= MeasurementStartBlock)
            {
                inputEnergy += SquareSum(microphone);
                cleanedEnergy += SquareSum(cleaned);
                residualEnergy += SquareSum(residual);
            }
        }

        const StereoCrosstalkCancellerSnapshot snapshot =
            canceller.GetSnapshot();
        const std::size_t delayDifference = snapshot.delaySamples >
            SyntheticDelaySamples
            ? snapshot.delaySamples - SyntheticDelaySamples
            : SyntheticDelaySamples - snapshot.delaySamples;
        const double linearReductionDb = 10.0 * std::log10(
            inputEnergy / std::max(cleanedEnergy, 1.0e-18)
        );
        const double residualReductionDb = 10.0 * std::log10(
            inputEnergy / std::max(residualEnergy, 1.0e-18)
        );

        if (!snapshot.delayLocked || delayDifference > 20 ||
            linearReductionDb < 8.0 || residualReductionDb < 24.0)
        {
            std::cerr
                << "delay=" << snapshot.delaySamples
                << " confidence=" << snapshot.delayConfidence
                << " linearReductionDb=" << linearReductionDb
                << " residualReductionDb=" << residualReductionDb
                << '\n';
            return false;
        }

        return snapshot.active && !snapshot.nearEndDetected;
    }

    bool TestNearEndSpeechReleasesResidualGate()
    {
        StereoCrosstalkCanceller canceller;
        SyntheticStream stream;
        std::array<float,
            StereoCrosstalkCanceller::RenderSamplesPerBlock> render{};
        std::array<float,
            StereoCrosstalkCanceller::SamplesPerBlock> microphone{};
        std::array<float,
            StereoCrosstalkCanceller::SamplesPerBlock> cleaned{};
        std::array<float,
            StereoCrosstalkCanceller::SamplesPerBlock> residual{};

        for (std::size_t block = 0; block < 280; ++block)
        {
            stream.FillBlock(render, microphone, false);
            if (!canceller.ProcessBlock(render, microphone, cleaned))
            {
                return false;
            }
            residual = cleaned;
            canceller.ApplyResidualSuppression(residual);
        }

        double nearEndEnergy = 0.0;
        double outputEnergy = 0.0;
        constexpr std::size_t DoubleTalkBlocks = 12;

        for (std::size_t block = 0; block < DoubleTalkBlocks; ++block)
        {
            stream.FillBlock(render, microphone, true);
            if (!canceller.ProcessBlock(render, microphone, cleaned))
            {
                return false;
            }
            residual = cleaned;
            canceller.ApplyResidualSuppression(residual);
            nearEndEnergy += SquareSum(microphone);
            outputEnergy += SquareSum(residual);
        }

        const StereoCrosstalkCancellerSnapshot snapshot =
            canceller.GetSnapshot();
        const double preservedRatio = std::sqrt(
            outputEnergy / std::max(nearEndEnergy, 1.0e-18)
        );

        return snapshot.nearEndDetected &&
            snapshot.residualGain > 0.99f &&
            preservedRatio > 0.72;
    }

    bool TestRenderSilenceBypasses()
    {
        StereoCrosstalkCanceller canceller;
        std::array<float,
            StereoCrosstalkCanceller::RenderSamplesPerBlock> render{};
        std::array<float,
            StereoCrosstalkCanceller::SamplesPerBlock> microphone{};
        std::array<float,
            StereoCrosstalkCanceller::SamplesPerBlock> output{};

        for (std::size_t index = 0; index < microphone.size(); ++index)
        {
            microphone[index] = 0.2f * std::sin(
                TwoPi * 220.0f * static_cast<float>(index) /
                static_cast<float>(
                    StereoCrosstalkCanceller::ProcessingSampleRate
                )
            );
        }

        if (!canceller.ProcessBlock(render, microphone, output))
        {
            return false;
        }
        canceller.ApplyResidualSuppression(output);

        return std::equal(
            microphone.begin(),
            microphone.end(),
            output.begin(),
            [](const float left, const float right)
            {
                return std::abs(left - right) < 0.000001f;
            }
        );
    }
}

int main()
{
    if (!TestInvalidShapeSafelyBypasses())
    {
        std::cerr << "FAILED: invalid shape did not safely bypass\n";
        return 1;
    }

    if (!TestAutomaticDelayAndLinearCancellation())
    {
        std::cerr << "FAILED: automatic crosstalk cancellation\n";
        return 1;
    }

    if (!TestNearEndSpeechReleasesResidualGate())
    {
        std::cerr << "FAILED: near-end speech was not preserved\n";
        return 1;
    }

    if (!TestRenderSilenceBypasses())
    {
        std::cerr << "FAILED: render silence did not bypass\n";
        return 1;
    }

    std::cout
        << "Stereo crosstalk canceller tests passed: delay tracking, "
           "adaptive cancellation, residual suppression, double-talk, and "
           "safe bypass.\n";
    return 0;
}
