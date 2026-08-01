#include "audio/VoicePolishEngine2.hpp"

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
        if (!condition)
        {
            ++failureCount;
            std::cerr << "FAILED: " << message << '\n';
        }
    }

    SpeechAnalysisFrame SpeechAnalysis(const float activity = 1.0f)
    {
        SpeechAnalysisFrame analysis;
        analysis.fundamentalFrequencyHz = 125.0f;
        analysis.pitchPeriodSamples = 384.0f;
        analysis.pitchConfidence = 0.95f;
        analysis.voicingConfidence = 0.90f;
        analysis.speechActivity = activity;
        analysis.rmsLevel = 0.08f;
        analysis.peakLevel = 0.22f;
        return analysis;
    }

    double Rms(const std::vector<float>& samples, const std::size_t start)
    {
        double sum = 0.0;
        for (std::size_t index = start; index < samples.size(); ++index)
        {
            const double sample = samples[index];
            sum += sample * sample;
        }
        return std::sqrt(sum /
            static_cast<double>(samples.size() - start));
    }

    std::vector<float> RenderTone(
        VoiceEffectSettings settings,
        const float frequencyHz,
        const float amplitude,
        const std::size_t sampleCount = 96000U
    )
    {
        VoicePolishEngine2 engine;
        engine.UpdateSettings(settings);
        engine.UpdateAnalysis(SpeechAnalysis());
        std::vector<float> output(sampleCount);
        for (std::size_t index = 0; index < sampleCount; ++index)
        {
            const float sample = amplitude * std::sin(
                2.0f * std::numbers::pi_v<float> * frequencyHz *
                static_cast<float>(index) /
                static_cast<float>(VoicePolishEngine2::ProcessingSampleRate)
            );
            output[index] = engine.ProcessSample(sample);
        }
        return output;
    }

    void TestDisabledChainIsTransparent()
    {
        VoicePolishEngine2 engine;
        VoiceEffectSettings settings;
        engine.UpdateSettings(settings);
        engine.UpdateAnalysis(SpeechAnalysis());
        for (std::size_t index = 0; index < 48000U; ++index)
        {
            const float sample = static_cast<float>(
                0.3 * std::sin(2.0 * std::numbers::pi * 431.0 *
                    static_cast<double>(index) / 48000.0)
            );
            Expect(engine.ProcessSample(sample) == sample,
                "disabled polish chain remains sample-exact transparent");
        }
    }

    void TestParametricEqBands()
    {
        VoiceEffectSettings neutral;
        VoiceEffectSettings shaped;
        shaped.parametricEqEnabled = true;
        shaped.eqLowGainDb = 6.0f;
        shaped.eqMidGainDb = -6.0f;
        shaped.eqHighGainDb = 6.0f;

        const auto neutralLow = RenderTone(neutral, 110.0f, 0.05f);
        const auto shapedLow = RenderTone(shaped, 110.0f, 0.05f);
        const auto neutralMid = RenderTone(neutral, 1450.0f, 0.05f);
        const auto shapedMid = RenderTone(shaped, 1450.0f, 0.05f);
        const auto neutralHigh = RenderTone(neutral, 8500.0f, 0.05f);
        const auto shapedHigh = RenderTone(shaped, 8500.0f, 0.05f);
        constexpr std::size_t start = 48000U;

        Expect(Rms(shapedLow, start) > Rms(neutralLow, start) * 1.45,
            "low shelf applies the requested bass boost");
        Expect(Rms(shapedMid, start) < Rms(neutralMid, start) * 0.62,
            "mid parametric band applies the requested cut");
        Expect(Rms(shapedHigh, start) > Rms(neutralHigh, start) * 1.45,
            "high shelf applies the requested air boost");
    }

    void TestParametricEqFrequencyAndQAreTunable()
    {
        VoiceEffectSettings focused;
        focused.parametricEqEnabled = true;
        focused.eqMidGainDb = -9.0f;
        focused.eqMidFrequencyHz = 900.0f;
        focused.eqMidQ = 3.0f;

        VoiceEffectSettings displaced = focused;
        displaced.eqMidFrequencyHz = 2600.0f;

        const auto focusedTone = RenderTone(focused, 900.0f, 0.05f);
        const auto displacedTone = RenderTone(displaced, 900.0f, 0.05f);
        constexpr std::size_t start = 48000U;

        Expect(
            Rms(focusedTone, start) < Rms(displacedTone, start) * 0.62,
            "mid-band frequency and Q move the parametric notch"
        );

        VoiceEffectSettings lowShelf = focused;
        lowShelf.eqMidGainDb = 0.0f;
        lowShelf.eqLowGainDb = 6.0f;
        lowShelf.eqLowFrequencyHz = 300.0f;
        VoiceEffectSettings lowShelfMoved = lowShelf;
        lowShelfMoved.eqLowFrequencyHz = 70.0f;
        const auto broadLow = RenderTone(lowShelf, 180.0f, 0.05f);
        const auto narrowLow = RenderTone(lowShelfMoved, 180.0f, 0.05f);
        Expect(
            Rms(broadLow, start) > Rms(narrowLow, start) * 1.22,
            "low-shelf frequency changes the bass transition"
        );
    }

    void TestDeEsserIsBandSelective()
    {
        VoiceEffectSettings settings;
        settings.deEsserEnabled = true;
        settings.deEsserAmount = 1.0f;
        const auto low = RenderTone(settings, 1000.0f, 0.18f);
        const auto high = RenderTone(settings, 7600.0f, 0.18f);
        constexpr std::size_t start = 48000U;
        const double lowRms = Rms(low, start);
        const double highRms = Rms(high, start);
        const double inputRms = 0.18 / std::sqrt(2.0);

        Expect(lowRms > inputRms * 0.92,
            "de-esser preserves non-sibilant speech frequencies");
        Expect(highRms < inputRms * 0.62,
            "de-esser attenuates sustained sibilance");
    }

    void TestGateExpanderAndSpeechProtection()
    {
        VoiceEffectSettings settings;
        settings.gateEnabled = true;
        settings.gateAmount = 1.0f;

        VoicePolishEngine2 noiseGate;
        noiseGate.UpdateSettings(settings);
        noiseGate.UpdateAnalysis(SpeechAnalysis(0.0f));
        std::vector<float> noise(96000U);
        for (std::size_t index = 0; index < noise.size(); ++index)
        {
            noise[index] = noiseGate.ProcessSample(
                index % 2U == 0U ? 0.001f : -0.001f
            );
        }

        VoicePolishEngine2 speechGate;
        speechGate.UpdateSettings(settings);
        speechGate.UpdateAnalysis(SpeechAnalysis(1.0f));
        std::vector<float> speech(96000U);
        for (std::size_t index = 0; index < speech.size(); ++index)
        {
            speech[index] = speechGate.ProcessSample(
                0.08f * std::sin(
                    2.0f * std::numbers::pi_v<float> * 180.0f *
                    static_cast<float>(index) / 48000.0f
                )
            );
        }

        Expect(Rms(noise, 48000U) < 0.00012,
            "gate strongly expands low-level background noise downward");
        Expect(Rms(speech, 48000U) > 0.052,
            "speech activity opens the gate without clipping syllables");
    }

    void TestGateOnsetProtectionOpensAndHolds()
    {
        VoiceEffectSettings settings;
        settings.gateEnabled = true;
        settings.gateAmount = 1.0f;

        VoicePolishEngine2 engine;
        engine.UpdateSettings(settings);
        engine.UpdateAnalysis(SpeechAnalysis(0.0f));
        for (std::size_t index = 0; index < 96000U; ++index)
        {
            static_cast<void>(engine.ProcessSample(
                index % 2U == 0U ? 0.001f : -0.001f
            ));
        }

        const float closedGain = engine.Metrics().gateGain;
        Expect(closedGain < 0.05f,
            "gate reaches its closed state before onset protection");

        SpeechAnalysisFrame onset = SpeechAnalysis(0.0f);
        onset.voicingConfidence = 0.0f;
        onset.onset = true;
        onset.transientProbability = 1.0f;
        engine.UpdateAnalysis(onset);
        for (std::size_t index = 0; index < 360U; ++index)
        {
            static_cast<void>(engine.ProcessSample(
                index % 2U == 0U ? 0.001f : -0.001f
            ));
        }

        const float openedGain = engine.Metrics().gateGain;
        Expect(openedGain > 0.90f,
            "onset protection fully opens an already closed gate");
        Expect(openedGain > closedGain * 15.0f,
            "onset protection overrides low-level attenuation");

        engine.UpdateAnalysis(SpeechAnalysis(0.0f));
        for (std::size_t index = 0; index < 1200U; ++index)
        {
            static_cast<void>(engine.ProcessSample(
                index % 2U == 0U ? 0.001f : -0.001f
            ));
        }
        Expect(engine.Metrics().gateGain > 0.88f,
            "gate hold preserves the beginning of a detected syllable");
    }

    void TestCompressorControlsLoudPeaks()
    {
        VoiceEffectSettings settings;
        settings.compressorEnabled = true;
        settings.compressorAmount = 1.0f;
        const auto quiet = RenderTone(settings, 300.0f, 0.03f);
        const auto loud = RenderTone(settings, 300.0f, 0.80f);
        constexpr std::size_t start = 48000U;

        const double quietGain = Rms(quiet, start) /
            (0.03 / std::sqrt(2.0));
        const double loudGain = Rms(loud, start) /
            (0.80 / std::sqrt(2.0));
        Expect(quietGain > 1.05,
            "compressor auto makeup keeps quiet voice present");
        Expect(loudGain < 0.58,
            "compressor applies strong gain reduction to loud voice");
    }

    void TestRackOrderChangesProcessingAndTransitionsSafely()
    {
        VoiceEffectSettings eqThenCompressor;
        eqThenCompressor.parametricEqEnabled = true;
        eqThenCompressor.compressorEnabled = true;
        eqThenCompressor.eqMidGainDb = 12.0f;
        eqThenCompressor.eqMidFrequencyHz = 1450.0f;
        eqThenCompressor.eqMidQ = 1.4f;
        eqThenCompressor.compressorAmount = 1.0f;
        eqThenCompressor.rackOrder = {
            VoiceEffectRackModule::ParametricEq,
            VoiceEffectRackModule::Compressor,
            VoiceEffectRackModule::DeEsser,
            VoiceEffectRackModule::Gate
        };

        VoiceEffectSettings compressorThenEq = eqThenCompressor;
        compressorThenEq.rackOrder = {
            VoiceEffectRackModule::Compressor,
            VoiceEffectRackModule::ParametricEq,
            VoiceEffectRackModule::DeEsser,
            VoiceEffectRackModule::Gate
        };

        const auto beforeCompression = RenderTone(
            eqThenCompressor, 1450.0f, 0.30f
        );
        const auto afterCompression = RenderTone(
            compressorThenEq, 1450.0f, 0.30f
        );
        constexpr std::size_t settled = 48000U;
        Expect(
            Rms(afterCompression, settled) >
                Rms(beforeCompression, settled) * 1.18,
            "rack order changes the expected nonlinear DSP result"
        );

        VoicePolishEngine2 engine;
        engine.UpdateSettings(eqThenCompressor);
        engine.UpdateAnalysis(SpeechAnalysis());
        float previous = 0.0f;
        float maximumDelta = 0.0f;
        bool finite = true;
        for (std::size_t index = 0; index < 96000U; ++index)
        {
            if (index == 48000U)
            {
                engine.UpdateSettings(compressorThenEq);
            }
            const float input = 0.22f * std::sin(
                2.0f * std::numbers::pi_v<float> * 330.0f *
                static_cast<float>(index) / 48000.0f
            );
            const float output = engine.ProcessSample(input);
            finite = finite && std::isfinite(output);
            if (index > 0U)
            {
                maximumDelta = std::max(
                    maximumDelta,
                    std::abs(output - previous)
                );
            }
            previous = output;
        }
        Expect(finite, "live rack reorder remains finite");
        Expect(
            maximumDelta < 0.35f,
            "live rack reorder uses a bounded dry transition"
        );
    }

    void TestFiniteBoundedAndReset()
    {
        VoicePolishEngine2 engine;
        VoiceEffectSettings settings;
        settings.parametricEqEnabled = true;
        settings.deEsserEnabled = true;
        settings.gateEnabled = true;
        settings.compressorEnabled = true;
        settings.eqLowGainDb = 99.0f;
        settings.eqLowFrequencyHz = -500.0f;
        settings.eqMidGainDb = -99.0f;
        settings.eqMidFrequencyHz =
            std::numeric_limits<float>::infinity();
        settings.eqMidQ = std::numeric_limits<float>::quiet_NaN();
        settings.eqHighGainDb = std::numeric_limits<float>::infinity();
        settings.eqHighFrequencyHz = 50000.0f;
        settings.deEsserAmount = 4.0f;
        settings.gateAmount = -2.0f;
        settings.compressorAmount = std::numeric_limits<float>::quiet_NaN();
        engine.UpdateSettings(settings);
        SpeechAnalysisFrame analysis;
        analysis.speechActivity = std::numeric_limits<float>::infinity();
        analysis.voicingConfidence = std::numeric_limits<float>::quiet_NaN();
        engine.UpdateAnalysis(analysis);

        for (std::size_t index = 0; index < 96000U; ++index)
        {
            const float input = index % 43U == 0U
                ? std::numeric_limits<float>::quiet_NaN()
                : (index % 2U == 0U ? 8.0f : -8.0f);
            const float output = engine.ProcessSample(input);
            Expect(std::isfinite(output),
                "invalid and overloaded samples always produce finite output");
            Expect(std::abs(output) <= 4.0f,
                "polish chain output remains explicitly bounded");
        }

        engine.Reset();
        Expect(engine.Metrics().gateGain == 1.0f,
            "reset restores unity gate gain");
        Expect(engine.Metrics().deEsserReductionDb == 0.0f,
            "reset clears de-esser reduction");
        Expect(engine.Metrics().compressorReductionDb == 0.0f,
            "reset clears compressor reduction");
    }
}

int main()
{
    TestDisabledChainIsTransparent();
    TestParametricEqBands();
    TestParametricEqFrequencyAndQAreTunable();
    TestDeEsserIsBandSelective();
    TestGateExpanderAndSpeechProtection();
    TestGateOnsetProtectionOpensAndHolds();
    TestCompressorControlsLoudPeaks();
    TestRackOrderChangesProcessingAndTransitionsSafely();
    TestFiniteBoundedAndReset();

    if (failureCount != 0)
    {
        std::cerr << failureCount << " VoicePolishEngine2 test(s) failed.\n";
        return 1;
    }

    std::cout << "VoicePolishEngine2 tests passed.\n";
    return 0;
}
