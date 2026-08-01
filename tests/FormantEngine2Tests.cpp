#include "audio/FormantEngine2.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>

namespace
{
    int failureCount = 0;

    using Spectrum = std::array<float, FormantEngine2::BinCount>;

    void Expect(const bool condition, const std::string_view message)
    {
        if (condition)
        {
            return;
        }

        ++failureCount;
        std::cerr << "FAILED: " << message << '\n';
    }

    float Gaussian(
        const float frequencyHz,
        const float centerHz,
        const float widthHz
    )
    {
        const float distance = (frequencyHz - centerHz) / widthHz;
        return std::exp(-0.5f * distance * distance);
    }

    void BuildVoicedSpectrum(Spectrum& magnitude, Spectrum& envelope)
    {
        constexpr float FrequencyPerBin =
            static_cast<float>(FormantEngine2::ProcessingSampleRate) /
            static_cast<float>(FormantEngine2::FrameSize);
        constexpr float FundamentalHz = 120.0f;

        for (std::size_t bin = 0; bin < FormantEngine2::BinCount; ++bin)
        {
            const float frequencyHz = static_cast<float>(bin) *
                FrequencyPerBin;
            const float tilt = 1.0f /
                std::sqrt(1.0f + frequencyHz / 650.0f);
            envelope[bin] = 0.015f + tilt * (
                0.80f * Gaussian(frequencyHz, 520.0f, 125.0f) +
                0.62f * Gaussian(frequencyHz, 1480.0f, 190.0f) +
                0.48f * Gaussian(frequencyHz, 2550.0f, 260.0f)
            );

            const float harmonic = std::round(frequencyHz / FundamentalHz);
            const float harmonicFrequency = harmonic * FundamentalHz;
            const float harmonicDistance = std::abs(
                frequencyHz - harmonicFrequency
            );
            const float harmonicWeight = harmonic >= 1.0f
                ? Gaussian(harmonicDistance, 0.0f, 22.0f)
                : 0.0f;
            magnitude[bin] = envelope[bin] *
                (0.015f + harmonicWeight);
        }
    }

    SpeechAnalysisFrame VoicedAnalysis()
    {
        SpeechAnalysisFrame analysis;
        analysis.fundamentalFrequencyHz = 120.0f;
        analysis.pitchPeriodSamples = 400.0f;
        analysis.pitchConfidence = 0.95f;
        analysis.voicingConfidence = 0.94f;
        analysis.speechActivity = 1.0f;
        analysis.rmsLevel = 0.12f;
        analysis.peakLevel = 0.35f;
        analysis.spectralFlatness = 0.16f;
        analysis.spectralCentroidHz = 1450.0f;
        analysis.highBandRatio = 0.08f;
        return analysis;
    }

    double CorrectedCentroid(
        const Spectrum& magnitude,
        const FormantEngine2& engine
    )
    {
        constexpr double FrequencyPerBin =
            static_cast<double>(FormantEngine2::ProcessingSampleRate) /
            static_cast<double>(FormantEngine2::FrameSize);
        double weighted = 0.0;
        double total = 0.0;
        for (std::size_t bin = 1; bin < FormantEngine2::BinCount; ++bin)
        {
            const double corrected = static_cast<double>(magnitude[bin]) *
                static_cast<double>(engine.CorrectionForBin(bin));
            const double energy = corrected * corrected;
            weighted += energy * static_cast<double>(bin) * FrequencyPerBin;
            total += energy;
        }
        return weighted / std::max(total, 0.000000000001);
    }

    float MaximumLogDeviation(const FormantEngine2& engine)
    {
        float maximum = 0.0f;
        for (const float correction : engine.Corrections())
        {
            maximum = std::max(maximum, std::abs(std::log(correction)));
        }
        return maximum;
    }

    void TestIndependentFormantShiftMovesEnvelope()
    {
        Spectrum magnitude{};
        Spectrum envelope{};
        BuildVoicedSpectrum(magnitude, envelope);
        const SpeechAnalysisFrame analysis = VoicedAnalysis();

        FormantEngine2 lower;
        FormantEngine2 higher;
        Expect(lower.PrepareFrame(
            magnitude,
            envelope,
            analysis,
            1.0f,
            std::exp2(-6.0f / 12.0f)
        ), "negative formant frame prepares");
        Expect(higher.PrepareFrame(
            magnitude,
            envelope,
            analysis,
            1.0f,
            std::exp2(6.0f / 12.0f)
        ), "positive formant frame prepares");

        const double lowCentroid = CorrectedCentroid(magnitude, lower);
        const double highCentroid = CorrectedCentroid(magnitude, higher);
        Expect(highCentroid > lowCentroid * 1.08,
            "positive formant shift raises the spectral-envelope centroid");
        Expect(lower.CurrentStrength() > 0.75f,
            "voiced negative shift engages the formant engine");
        Expect(higher.CurrentStrength() > 0.75f,
            "voiced positive shift engages the formant engine");
    }

    void TestMatchingPitchAndFormantStayNeutral()
    {
        Spectrum magnitude{};
        Spectrum envelope{};
        BuildVoicedSpectrum(magnitude, envelope);

        FormantEngine2 engine;
        const float ratio = std::exp2(5.0f / 12.0f);
        Expect(engine.PrepareFrame(
            magnitude,
            envelope,
            VoicedAnalysis(),
            ratio,
            ratio
        ), "matching pitch/formant frame prepares");

        float maximumDeviation = 0.0f;
        for (const float correction : engine.Corrections())
        {
            maximumDeviation = std::max(
                maximumDeviation,
                std::abs(correction - 1.0f)
            );
        }
        Expect(maximumDeviation < 0.0001f,
            "matching pitch and formant ratios need no envelope correction");
        Expect(engine.CurrentStrength() < 0.0001f,
            "neutral relative formant shift disengages the engine");
    }

    void TestTransientAndUnvoicedProtection()
    {
        Spectrum magnitude{};
        Spectrum envelope{};
        BuildVoicedSpectrum(magnitude, envelope);

        SpeechAnalysisFrame voiced = VoicedAnalysis();
        SpeechAnalysisFrame protectedFrame = voiced;
        protectedFrame.voicingConfidence = 0.05f;
        protectedFrame.unvoicedProbability = 0.96f;
        protectedFrame.transientProbability = 0.92f;
        protectedFrame.onset = true;

        FormantEngine2 voicedEngine;
        FormantEngine2 protectedEngine;
        const float ratio = std::exp2(6.0f / 12.0f);
        Expect(voicedEngine.PrepareFrame(
            magnitude,
            envelope,
            voiced,
            1.0f,
            ratio
        ), "voiced protection reference prepares");
        Expect(protectedEngine.PrepareFrame(
            magnitude,
            envelope,
            protectedFrame,
            1.0f,
            ratio
        ), "unvoiced transient frame prepares");

        Expect(protectedEngine.CurrentStrength() <
            voicedEngine.CurrentStrength() * 0.15f,
            "unvoiced transient analysis strongly reduces formant warping");
        Expect(MaximumLogDeviation(protectedEngine) <
            MaximumLogDeviation(voicedEngine) * 0.35f,
            "protected consonants keep a substantially flatter correction");
    }

    void TestEnergyAndCorrectionBounds()
    {
        Spectrum magnitude{};
        Spectrum envelope{};
        BuildVoicedSpectrum(magnitude, envelope);
        envelope[10] = 0.0000000001f;
        envelope[80] = 50.0f;

        FormantEngine2 engine;
        Expect(engine.PrepareFrame(
            magnitude,
            envelope,
            VoicedAnalysis(),
            0.5f,
            2.0f
        ), "extreme valid ratio frame prepares");

        Expect(engine.CurrentEnergyGain() >= 0.82f &&
            engine.CurrentEnergyGain() <= 1.20f,
            "frame energy normalization stays bounded");
        for (const float correction : engine.Corrections())
        {
            Expect(std::isfinite(correction),
                "every correction remains finite");
            Expect(correction >= 0.48f && correction <= 2.10f,
                "every correction stays inside the resonance-safe bounds");
        }
    }

    void TestInvalidInputAndReset()
    {
        Spectrum magnitude{};
        Spectrum envelope{};
        BuildVoicedSpectrum(magnitude, envelope);

        FormantEngine2 engine;
        Expect(!engine.PrepareFrame(
            std::span<const float>(magnitude.data(), magnitude.size() - 1U),
            envelope,
            VoicedAnalysis(),
            1.0f,
            1.0f
        ), "short magnitude spectrum is rejected");

        magnitude[4] = std::numeric_limits<float>::quiet_NaN();
        envelope[8] = std::numeric_limits<float>::infinity();
        Expect(engine.PrepareFrame(
            magnitude,
            envelope,
            VoicedAnalysis(),
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity()
        ), "non-finite frame values are sanitized");
        for (const float correction : engine.Corrections())
        {
            Expect(std::isfinite(correction),
                "sanitized frame produces finite corrections");
        }

        engine.Reset();
        Expect(engine.CurrentStrength() == 0.0f,
            "reset clears formant strength");
        Expect(engine.CurrentEnergyGain() == 1.0f,
            "reset restores unity energy gain");
        for (const float correction : engine.Corrections())
        {
            Expect(correction == 1.0f,
                "reset restores unity correction");
        }
        Expect(engine.CorrectionForBin(FormantEngine2::BinCount + 5U) == 1.0f,
            "out-of-range correction lookup is transparent");
    }
}

int main()
{
    TestIndependentFormantShiftMovesEnvelope();
    TestMatchingPitchAndFormantStayNeutral();
    TestTransientAndUnvoicedProtection();
    TestEnergyAndCorrectionBounds();
    TestInvalidInputAndReset();

    if (failureCount != 0)
    {
        std::cerr << failureCount << " FormantEngine2 test(s) failed.\n";
        return 1;
    }

    std::cout << "FormantEngine2 tests passed.\n";
    return 0;
}
