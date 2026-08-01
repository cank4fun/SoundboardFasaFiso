#include "audio/VocalWeightEngine2.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <numbers>
#include <string_view>
#include <vector>

namespace
{
    int failureCount = 0;

    void Expect(const bool condition, const std::string_view message)
    {
        if (condition)
        {
            return;
        }

        ++failureCount;
        std::cerr << "FAILED: " << message << '\n';
    }

    SpeechAnalysisFrame VoicedAnalysis(const float fundamentalHz = 120.0f)
    {
        SpeechAnalysisFrame analysis;
        analysis.fundamentalFrequencyHz = fundamentalHz;
        analysis.pitchPeriodSamples =
            static_cast<float>(VocalWeightEngine2::ProcessingSampleRate) /
            fundamentalHz;
        analysis.pitchConfidence = 0.96f;
        analysis.voicingConfidence = 0.95f;
        analysis.speechActivity = 1.0f;
        analysis.rmsLevel = 0.09f;
        analysis.peakLevel = 0.25f;
        analysis.spectralFlatness = 0.12f;
        analysis.spectralCentroidHz = 1250.0f;
        analysis.highBandRatio = 0.06f;
        return analysis;
    }

    std::vector<float> Render(
        const float amount,
        const SpeechAnalysisFrame& analysis,
        const std::vector<double>& frequencies,
        const std::size_t sampleCount = 96000U
    )
    {
        VocalWeightEngine2 engine;
        engine.UpdateAnalysis(analysis);
        std::vector<float> output(sampleCount, 0.0f);
        for (std::size_t index = 0; index < sampleCount; ++index)
        {
            double sample = 0.0;
            for (const double frequency : frequencies)
            {
                const double phase = 2.0 * std::numbers::pi * frequency *
                    static_cast<double>(index) /
                    static_cast<double>(
                        VocalWeightEngine2::ProcessingSampleRate
                    );
                sample += 0.06 * std::sin(phase);
            }
            output[index] = engine.ProcessSample(
                static_cast<float>(sample),
                amount
            );
        }
        return output;
    }

    double ToneMagnitude(
        const std::vector<float>& samples,
        const std::size_t start,
        const std::size_t count,
        const double frequencyHz
    )
    {
        double real = 0.0;
        double imaginary = 0.0;
        for (std::size_t index = 0; index < count; ++index)
        {
            const double phase = 2.0 * std::numbers::pi * frequencyHz *
                static_cast<double>(index) /
                static_cast<double>(
                    VocalWeightEngine2::ProcessingSampleRate
                );
            const double sample = samples[start + index];
            real += sample * std::cos(phase);
            imaginary -= sample * std::sin(phase);
        }
        return 2.0 * std::hypot(real, imaginary) /
            static_cast<double>(count);
    }

    double DifferenceRms(
        const std::vector<float>& processed,
        const std::vector<float>& neutral,
        const std::size_t start
    )
    {
        double sum = 0.0;
        const std::size_t count = processed.size() - start;
        for (std::size_t index = start; index < processed.size(); ++index)
        {
            const double difference = static_cast<double>(processed[index]) -
                static_cast<double>(neutral[index]);
            sum += difference * difference;
        }
        return std::sqrt(sum / static_cast<double>(count));
    }

    void TestZeroAmountIsTransparent()
    {
        VocalWeightEngine2 engine;
        engine.UpdateAnalysis(VoicedAnalysis());
        for (std::size_t index = 0; index < 24000U; ++index)
        {
            const float sample = static_cast<float>(
                0.2 * std::sin(
                    2.0 * std::numbers::pi * 173.0 *
                    static_cast<double>(index) /
                    static_cast<double>(
                        VocalWeightEngine2::ProcessingSampleRate
                    )
                )
            );
            const float output = engine.ProcessSample(sample, 0.0f);
            Expect(output == sample,
                "zero vocal weight remains sample-exact transparent");
        }
    }

    void TestWeightAddsChestAndControlsBoxiness()
    {
        const std::vector<double> frequencies{140.0, 520.0, 1800.0};
        const std::vector<float> neutral = Render(
            0.0f,
            VoicedAnalysis(140.0f),
            frequencies
        );
        const std::vector<float> weighted = Render(
            1.0f,
            VoicedAnalysis(140.0f),
            frequencies
        );
        constexpr std::size_t AnalysisCount = 24000U;
        const std::size_t start = neutral.size() - AnalysisCount;

        const double neutralChest = ToneMagnitude(
            neutral, start, AnalysisCount, 140.0
        );
        const double weightedChest = ToneMagnitude(
            weighted, start, AnalysisCount, 140.0
        );
        const double neutralBox = ToneMagnitude(
            neutral, start, AnalysisCount, 520.0
        );
        const double weightedBox = ToneMagnitude(
            weighted, start, AnalysisCount, 520.0
        );
        const double neutralPresence = ToneMagnitude(
            neutral, start, AnalysisCount, 1800.0
        );
        const double weightedPresence = ToneMagnitude(
            weighted, start, AnalysisCount, 1800.0
        );

        Expect(weightedChest > neutralChest * 1.35,
            "vocal weight reinforces the pitch-coherent chest band");
        Expect(weightedBox < neutralBox * 0.94,
            "vocal weight suppresses box-band buildup");
        Expect(weightedPresence > neutralPresence * 0.96,
            "vocal weight preserves speech presence");
    }

    void TestAnalysisAdaptsChestCenter()
    {
        VocalWeightEngine2 lowVoice;
        VocalWeightEngine2 highVoice;
        lowVoice.UpdateAnalysis(VoicedAnalysis(90.0f));
        highVoice.UpdateAnalysis(VoicedAnalysis(260.0f));
        for (std::size_t index = 0; index < 48000U; ++index)
        {
            (void)lowVoice.ProcessSample(0.0f, 1.0f);
            (void)highVoice.ProcessSample(0.0f, 1.0f);
        }

        Expect(lowVoice.Metrics().chestCenterFrequencyHz <
            highVoice.Metrics().chestCenterFrequencyHz - 45.0f,
            "F0 analysis moves the chest contour with the speaker");
        Expect(lowVoice.Metrics().chestCenterFrequencyHz >= 125.0f &&
            highVoice.Metrics().chestCenterFrequencyHz <= 235.0f,
            "adaptive chest contour stays inside the vocal-safe range");
    }

    void TestTransientAndUnvoicedProtection()
    {
        SpeechAnalysisFrame protectedAnalysis = VoicedAnalysis(140.0f);
        protectedAnalysis.voicingConfidence = 0.03f;
        protectedAnalysis.unvoicedProbability = 0.98f;
        protectedAnalysis.transientProbability = 0.96f;
        protectedAnalysis.onset = true;

        const std::vector<double> frequencies{140.0, 700.0, 3200.0};
        const std::vector<float> neutral = Render(
            0.0f,
            VoicedAnalysis(140.0f),
            frequencies
        );
        const std::vector<float> voiced = Render(
            1.0f,
            VoicedAnalysis(140.0f),
            frequencies
        );
        const std::vector<float> protectedOutput = Render(
            1.0f,
            protectedAnalysis,
            frequencies
        );
        const std::size_t start = neutral.size() - 24000U;
        const double voicedDifference = DifferenceRms(voiced, neutral, start);
        const double protectedDifference = DifferenceRms(
            protectedOutput,
            neutral,
            start
        );

        Expect(protectedDifference < voicedDifference * 0.38,
            "unvoiced transients strongly reduce weight processing");
    }

    void TestFiniteBoundedOutputAndReset()
    {
        VocalWeightEngine2 engine;
        SpeechAnalysisFrame analysis = VoicedAnalysis();
        analysis.fundamentalFrequencyHz =
            std::numeric_limits<float>::quiet_NaN();
        analysis.pitchConfidence = std::numeric_limits<float>::infinity();
        analysis.voicingConfidence =
            -std::numeric_limits<float>::infinity();
        engine.UpdateAnalysis(analysis);

        float maximumAbsolute = 0.0f;
        for (std::size_t index = 0; index < 96000U; ++index)
        {
            const float sample = index % 37U == 0U
                ? std::numeric_limits<float>::quiet_NaN()
                : static_cast<float>(
                    1.8 * std::sin(
                        2.0 * std::numbers::pi * 125.0 *
                        static_cast<double>(index) /
                        static_cast<double>(
                            VocalWeightEngine2::ProcessingSampleRate
                        )
                    )
                );
            const float output = engine.ProcessSample(sample, 4.0f);
            Expect(std::isfinite(output),
                "non-finite and excessive inputs produce finite output");
            maximumAbsolute = std::max(maximumAbsolute, std::abs(output));
        }
        Expect(maximumAbsolute <= 2.19f,
            "parallel contribution remains bounded under overload");

        engine.Reset();
        Expect(engine.Metrics().voiceGate == 0.0f,
            "reset clears the voice gate");
        Expect(engine.Metrics().contribution == 0.0f,
            "reset clears the parallel contribution");
        Expect(engine.Metrics().chestCenterFrequencyHz == 165.0f,
            "reset restores the neutral chest center");
    }
}

int main()
{
    TestZeroAmountIsTransparent();
    TestWeightAddsChestAndControlsBoxiness();
    TestAnalysisAdaptsChestCenter();
    TestTransientAndUnvoicedProtection();
    TestFiniteBoundedOutputAndReset();

    if (failureCount != 0)
    {
        std::cerr << failureCount << " VocalWeightEngine2 test(s) failed.\n";
        return 1;
    }

    std::cout << "VocalWeightEngine2 tests passed.\n";
    return 0;
}
