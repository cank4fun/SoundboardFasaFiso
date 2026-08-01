#include "audio/PitchEngine2.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <numbers>
#include <vector>

namespace
{
    int failureCount = 0;

    void Expect(const bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failureCount;
        }
    }

    bool NearlyEqual(
        const float left,
        const float right,
        const float tolerance = 0.000001f
    )
    {
        return std::abs(left - right) <= tolerance;
    }

    double ToneMagnitude(
        const std::vector<float>& samples,
        const std::size_t start,
        const std::size_t count,
        const double frequency
    )
    {
        double real = 0.0;
        double imaginary = 0.0;
        for (std::size_t index = 0; index < count; ++index)
        {
            const double phase = 2.0 * std::numbers::pi * frequency *
                static_cast<double>(index) /
                static_cast<double>(PitchEngine2::ProcessingSampleRate);
            const double sample = samples[start + index];
            real += sample * std::cos(phase);
            imaginary -= sample * std::sin(phase);
        }
        return std::hypot(real, imaginary);
    }

    SpeechAnalysisFrame StrongVoicedFrame(const float pitchPeriod)
    {
        SpeechAnalysisFrame frame;
        frame.fundamentalFrequencyHz =
            static_cast<float>(PitchEngine2::ProcessingSampleRate) /
            pitchPeriod;
        frame.pitchPeriodSamples = pitchPeriod;
        frame.pitchConfidence = 0.96f;
        frame.voicingConfidence = 0.94f;
        frame.speechActivity = 1.0f;
        frame.rmsLevel = 0.12f;
        frame.peakLevel = 0.24f;
        frame.spectralFlatness = 0.04f;
        frame.spectralCentroidHz = 1100.0f;
        frame.highBandRatio = 0.08f;
        return frame;
    }

    void TestNeutralPathPreservesFixedLatency()
    {
        PitchEngine2 engine;
        constexpr std::size_t SampleCount = 1800;
        std::vector<float> delayed(SampleCount, 0.0f);

        for (std::size_t index = 0; index < SampleCount; ++index)
        {
            const float input = index == 0U ? 0.5f : 0.0f;
            delayed[index] = engine.ProcessSample(
                input,
                0.0f,
                1.0f,
                1.0f
            ).delayedDry;
        }

        for (std::size_t index = 0; index < delayed.size(); ++index)
        {
            const float expected =
                index == PitchEngine2::ProcessingLatencySamples
                    ? 0.5f
                    : 0.0f;
            Expect(NearlyEqual(delayed[index], expected),
                "neutral dry path preserves one fixed-latency impulse");
        }
    }

    void TestPitchSynchronousGrainSpanTracksEvenCycles()
    {
        PitchEngine2 engine;
        engine.UpdateAnalysis(StrongVoicedFrame(160.0f));

        for (std::size_t index = 0; index < 16000U; ++index)
        {
            static_cast<void>(engine.ProcessSample(
                0.0f,
                0.0f,
                1.25f,
                1.0f
            ));
        }

        Expect(std::abs(engine.CurrentGrainSpanSamples() - 960.0f) < 0.2f,
            "grain span converges to an even six-cycle voiced window");

        engine.UpdateAnalysis(StrongVoicedFrame(640.0f));
        for (std::size_t index = 0; index < 18000U; ++index)
        {
            static_cast<void>(engine.ProcessSample(
                0.0f,
                0.0f,
                0.75f,
                1.0f
            ));
        }

        Expect(std::abs(engine.CurrentGrainSpanSamples() - 1280.0f) < 0.2f,
            "low voices retain a complete even two-cycle grain window");
    }

    void TestVoicedPitchShiftMovesHarmonicEnergy()
    {
        PitchEngine2 engine;
        constexpr double FundamentalFrequency = 150.0;
        constexpr float PitchRatio = 1.2599210498948732f;
        constexpr double ShiftedFrequency =
            FundamentalFrequency * static_cast<double>(PitchRatio);
        constexpr std::size_t SampleCount = 96000;
        std::vector<float> rendered(SampleCount, 0.0f);
        SpeechAnalysisFrame frame = StrongVoicedFrame(320.0f);
        engine.UpdateAnalysis(frame);

        for (std::size_t index = 0; index < SampleCount; ++index)
        {
            if ((index % SpeechAnalysisCore::HopSize) == 0U)
            {
                engine.UpdateAnalysis(frame);
            }

            double sample = 0.0;
            for (std::size_t harmonic = 1; harmonic <= 12U; ++harmonic)
            {
                const double frequency = FundamentalFrequency *
                    static_cast<double>(harmonic);
                const double phase = 2.0 * std::numbers::pi * frequency *
                    static_cast<double>(index) /
                    static_cast<double>(PitchEngine2::ProcessingSampleRate);
                sample += 0.06 / static_cast<double>(harmonic) *
                    std::sin(phase);
            }

            rendered[index] = engine.ProcessSample(
                static_cast<float>(sample),
                0.0f,
                PitchRatio,
                1.0f
            ).timeDomainPitch;
        }

        constexpr std::size_t AnalysisCount = 36000;
        const std::size_t analysisStart = rendered.size() - AnalysisCount;
        const double originalMagnitude = ToneMagnitude(
            rendered,
            analysisStart,
            AnalysisCount,
            FundamentalFrequency
        );
        const double shiftedMagnitude = ToneMagnitude(
            rendered,
            analysisStart,
            AnalysisCount,
            ShiftedFrequency
        );

        Expect(shiftedMagnitude > originalMagnitude * 1.8,
            "pitch-synchronous grains move voiced energy to the target pitch");
        Expect(shiftedMagnitude > 120.0,
            "time-domain target pitch remains materially present");
    }

    void TestUnvoicedTransientProtectionWithdrawsVoicedBlend()
    {
        PitchEngine2 engine;
        SpeechAnalysisFrame voiced = StrongVoicedFrame(240.0f);
        engine.UpdateAnalysis(voiced);

        PitchEngine2Result result;
        for (std::size_t index = 0; index < 12000U; ++index)
        {
            result = engine.ProcessSample(
                0.1f * std::sin(
                    2.0f * std::numbers::pi_v<float> * 200.0f *
                    static_cast<float>(index) /
                    static_cast<float>(PitchEngine2::ProcessingSampleRate)
                ),
                0.05f,
                1.5f,
                1.0f
            );
        }
        Expect(result.voicedBlend > 0.55f,
            "strong voiced analysis enables the hybrid pitch path");

        SpeechAnalysisFrame consonant;
        consonant.speechActivity = 1.0f;
        consonant.rmsLevel = 0.08f;
        consonant.peakLevel = 0.22f;
        consonant.spectralFlatness = 0.85f;
        consonant.spectralCentroidHz = 5200.0f;
        consonant.highBandRatio = 0.78f;
        consonant.transientProbability = 1.0f;
        consonant.unvoicedProbability = 1.0f;
        consonant.onset = true;
        engine.UpdateAnalysis(consonant);

        for (std::size_t index = 0; index < 16000U; ++index)
        {
            result = engine.ProcessSample(
                (index & 1U) == 0U ? 0.12f : -0.12f,
                0.04f,
                1.5f,
                1.0f
            );
            Expect(std::isfinite(result.hybridPitch),
                "unvoiced transient output remains finite");
        }

        Expect(result.voicedBlend < 0.05f,
            "unvoiced transient analysis withdraws voiced grain blending");
        Expect(result.transientDryMix > 0.45f,
            "transient protection reaches the dry-preservation range");
        Expect(result.unvoicedDryMix > 0.92f,
            "unvoiced protection converges for consonant-like input");
    }

    void TestResetIsDeterministicAndInvalidValuesStayFinite()
    {
        PitchEngine2 engine;
        const SpeechAnalysisFrame frame = StrongVoicedFrame(266.66666f);
        std::array<float, 1200> reference{};
        std::array<float, 1200> repeated{};

        const auto render = [&](auto& destination)
        {
            engine.UpdateAnalysis(frame);
            for (std::size_t index = 0; index < destination.size(); ++index)
            {
                const float input = 0.11f * std::sin(
                    2.0f * std::numbers::pi_v<float> * 180.0f *
                    static_cast<float>(index) /
                    static_cast<float>(PitchEngine2::ProcessingSampleRate)
                );
                destination[index] = engine.ProcessSample(
                    input,
                    input,
                    1.334839854f,
                    0.9f
                ).hybridPitch;
            }
        };

        render(reference);
        engine.Reset();
        render(repeated);
        Expect(reference == repeated,
            "reset removes all hidden Pitch Engine 2 state");

        const PitchEngine2Result invalid = engine.ProcessSample(
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::quiet_NaN()
        );
        Expect(std::isfinite(invalid.delayedDry) &&
            std::isfinite(invalid.timeDomainPitch) &&
            std::isfinite(invalid.hybridPitch),
            "invalid sample and control values are sanitized");
    }
}

int main()
{
    TestNeutralPathPreservesFixedLatency();
    TestPitchSynchronousGrainSpanTracksEvenCycles();
    TestVoicedPitchShiftMovesHarmonicEnergy();
    TestUnvoicedTransientProtectionWithdrawsVoicedBlend();
    TestResetIsDeterministicAndInvalidValuesStayFinite();

    if (failureCount != 0)
    {
        std::cerr << failureCount << " test assertion(s) failed.\n";
        return 1;
    }

    std::cout << "Pitch Engine 2 tests passed.\n";
    return 0;
}
