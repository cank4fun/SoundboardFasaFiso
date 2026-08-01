#include "audio/SpeechAnalysisCore.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <span>
#include <string_view>

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

    using TimeFrame = std::array<float, SpeechAnalysisCore::FrameSize>;
    using Spectrum = std::array<float, SpeechAnalysisCore::BinCount>;

    void FillTonalSpectrum(Spectrum& spectrum, const float frequencyHz)
    {
        spectrum.fill(0.000001f);
        const float exactBin = frequencyHz *
            static_cast<float>(SpeechAnalysisCore::FrameSize) /
            static_cast<float>(SpeechAnalysisCore::ProcessingSampleRate);
        const std::size_t bin = std::clamp(
            static_cast<std::size_t>(std::lround(exactBin)),
            std::size_t{1},
            SpeechAnalysisCore::BinCount - 2U
        );
        spectrum[bin - 1U] = 0.32f;
        spectrum[bin] = 1.0f;
        spectrum[bin + 1U] = 0.32f;

        const std::size_t secondHarmonic = std::min(
            bin * 2U,
            SpeechAnalysisCore::BinCount - 2U
        );
        spectrum[secondHarmonic] = 0.28f;
    }

    void FillSine(
        TimeFrame& frame,
        const float frequencyHz,
        const std::uint64_t startSample,
        const float amplitude = 0.18f
    )
    {
        for (std::size_t index = 0; index < frame.size(); ++index)
        {
            const double phase = 2.0 * std::numbers::pi *
                static_cast<double>(frequencyHz) *
                static_cast<double>(startSample + index) /
                static_cast<double>(
                    SpeechAnalysisCore::ProcessingSampleRate
                );
            frame[index] = amplitude * static_cast<float>(std::sin(phase));
        }
    }

    float TrackFrequency(const float frequencyHz)
    {
        SpeechAnalysisCore analysis;
        TimeFrame frame{};
        Spectrum spectrum{};
        FillTonalSpectrum(spectrum, frequencyHz);

        std::uint64_t sampleSequence = 0;
        for (int iteration = 0; iteration < 28; ++iteration)
        {
            FillSine(frame, frequencyHz, sampleSequence);
            sampleSequence += SpeechAnalysisCore::HopSize;
            Expect(analysis.AnalyzeFrame(frame, spectrum),
                "voiced frame is accepted");
        }

        const SpeechAnalysisFrame result = analysis.LatestFrame();
        Expect(result.pitchConfidence > 0.65f,
            "stable tone produces strong pitch confidence");
        Expect(result.voicingConfidence > 0.55f,
            "stable tone produces strong voicing confidence");
        Expect(result.speechActivity > 0.75f,
            "stable tone is recognized as active speech");
        return result.fundamentalFrequencyHz;
    }

    void TestFrameContractAndSanitization()
    {
        SpeechAnalysisCore analysis;
        TimeFrame frame{};
        Spectrum spectrum{};
        spectrum.fill(0.0f);

        Expect(!analysis.AnalyzeFrame(
            std::span<const float>(frame.data(), frame.size() - 1U),
            spectrum
        ), "short time-domain frame is rejected");
        Expect(!analysis.AnalyzeFrame(
            frame,
            std::span<const float>(spectrum.data(), spectrum.size() - 1U)
        ), "short spectrum is rejected");

        frame[0] = std::numeric_limits<float>::quiet_NaN();
        frame[1] = std::numeric_limits<float>::infinity();
        spectrum[4] = std::numeric_limits<float>::quiet_NaN();
        spectrum[5] = -1.0f;
        Expect(analysis.AnalyzeFrame(frame, spectrum),
            "non-finite samples are sanitized");

        const SpeechAnalysisFrame result = analysis.LatestFrame();
        Expect(std::isfinite(result.rmsLevel),
            "sanitized RMS remains finite");
        Expect(std::isfinite(result.spectralFlatness),
            "sanitized spectral flatness remains finite");
        Expect(result.speechActivity == 0.0f,
            "sanitized silence remains inactive");
        Expect(result.unvoicedProbability == 0.0f,
            "silence is not misclassified as unvoiced speech");
    }

    void TestPitchRangeAndOctaveSelection()
    {
        const float lowFrequency = TrackFrequency(110.0f);
        Expect(std::abs(lowFrequency - 110.0f) < 3.0f,
            "analysis tracks low male-range fundamentals");

        const float highFrequency = TrackFrequency(440.0f);
        Expect(std::abs(highFrequency - 440.0f) < 7.0f,
            "analysis selects the first strong pitch peak instead of an octave");
    }

    void TestVoicedAndUnvoicedSeparation()
    {
        SpeechAnalysisCore voicedAnalysis;
        TimeFrame voicedFrame{};
        Spectrum voicedSpectrum{};
        FillTonalSpectrum(voicedSpectrum, 180.0f);
        for (int iteration = 0; iteration < 20; ++iteration)
        {
            FillSine(
                voicedFrame,
                180.0f,
                static_cast<std::uint64_t>(iteration) *
                    SpeechAnalysisCore::HopSize
            );
            Expect(voicedAnalysis.AnalyzeFrame(
                voicedFrame,
                voicedSpectrum
            ), "voiced comparison frame is accepted");
        }
        const SpeechAnalysisFrame voiced = voicedAnalysis.LatestFrame();

        SpeechAnalysisCore noiseAnalysis;
        TimeFrame noiseFrame{};
        Spectrum noiseSpectrum{};
        noiseSpectrum.fill(0.18f);
        std::uint32_t state = 0x12345678U;
        for (int iteration = 0; iteration < 20; ++iteration)
        {
            for (float& sample : noiseFrame)
            {
                state = state * 1664525U + 1013904223U;
                const float normalized = static_cast<float>(
                    static_cast<std::int32_t>(state)
                ) / static_cast<float>(
                    std::numeric_limits<std::int32_t>::max()
                );
                sample = normalized * 0.12f;
            }
            Expect(noiseAnalysis.AnalyzeFrame(
                noiseFrame,
                noiseSpectrum
            ), "unvoiced comparison frame is accepted");
        }
        const SpeechAnalysisFrame noise = noiseAnalysis.LatestFrame();

        Expect(voiced.voicingConfidence > noise.voicingConfidence + 0.35f,
            "periodicity separates voiced sound from broadband noise");
        Expect(noise.unvoicedProbability > voiced.unvoicedProbability + 0.35f,
            "flat broadband sound receives a stronger unvoiced probability");
        Expect(noise.spectralFlatness > 0.90f,
            "broadband spectrum reports high flatness");
        Expect(voiced.spectralFlatness < 0.20f,
            "tonal spectrum reports low flatness");
    }

    void TestTransientAndEnvelopeAnalysis()
    {
        SpeechAnalysisCore analysis;
        TimeFrame frame{};
        Spectrum spectrum{};
        spectrum.fill(0.000001f);
        Expect(analysis.AnalyzeFrame(frame, spectrum),
            "quiet baseline frame is accepted");

        frame.fill(0.0f);
        frame[SpeechAnalysisCore::FrameSize / 2U] = 0.9f;
        spectrum.fill(0.55f);
        Expect(analysis.AnalyzeFrame(frame, spectrum),
            "impulse frame is accepted");

        const SpeechAnalysisFrame transient = analysis.LatestFrame();
        Expect(transient.onset, "large energy step is marked as an onset");
        Expect(transient.transientProbability > 0.75f,
            "broadband onset receives strong transient probability");
        Expect(transient.spectralCentroidHz > 4000.0f,
            "broadband spectrum produces a high centroid");

        spectrum.fill(0.000001f);
        spectrum[20] = 1.0f;
        spectrum[21] = 0.8f;
        spectrum[22] = 0.6f;
        frame.fill(0.02f);
        Expect(analysis.AnalyzeFrame(frame, spectrum),
            "envelope probe frame is accepted");
        const auto envelope = analysis.SpectralEnvelope();
        Expect(envelope.size() == SpeechAnalysisCore::BinCount,
            "spectral envelope keeps the fixed FFT-bin contract");
        Expect(envelope[21] > envelope[120],
            "spectral envelope follows broad spectral energy");
        Expect(std::isfinite(analysis.SampleSpectralEnvelope(21.5f)),
            "interpolated spectral-envelope reads remain finite");
    }

    void TestResetIsDeterministic()
    {
        SpeechAnalysisCore analysis;
        TimeFrame frame{};
        Spectrum spectrum{};
        FillSine(frame, 220.0f, 0);
        FillTonalSpectrum(spectrum, 220.0f);

        for (int iteration = 0; iteration < 12; ++iteration)
        {
            Expect(analysis.AnalyzeFrame(frame, spectrum),
                "pre-reset voiced frame is accepted");
        }

        analysis.Reset();
        const SpeechAnalysisFrame reset = analysis.LatestFrame();
        Expect(reset.pitchConfidence == 0.0f,
            "reset clears pitch confidence");
        Expect(reset.voicingConfidence == 0.0f,
            "reset clears voicing confidence");
        Expect(reset.speechActivity == 0.0f,
            "reset clears activity");
        Expect(std::abs(reset.pitchPeriodSamples - 240.0f) < 0.000001f,
            "reset restores the neutral pitch period");
        Expect(std::all_of(
            analysis.SpectralEnvelope().begin(),
            analysis.SpectralEnvelope().end(),
            [](const float value)
            {
                return std::isfinite(value) && value > 0.0f;
            }
        ), "reset leaves a finite positive spectral envelope");
    }
}

int main()
{
    TestFrameContractAndSanitization();
    TestPitchRangeAndOctaveSelection();
    TestVoicedAndUnvoicedSeparation();
    TestTransientAndEnvelopeAnalysis();
    TestResetIsDeterministic();

    if (failureCount != 0)
    {
        std::cerr << failureCount << " test assertion(s) failed.\n";
        return 1;
    }

    std::cout << "Speech-analysis core tests passed.\n";
    return 0;
}
