#include "audio/RnNoiseSuppressor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>

namespace
{
    bool IsFiniteFrame(
        const std::array<float, RnNoiseSuppressor::SamplesPerFrame>& frame
    )
    {
        return std::all_of(
            frame.begin(),
            frame.end(),
            [](const float sample)
            {
                return std::isfinite(sample) &&
                    sample >= -1.0f && sample <= 1.0f;
            }
        );
    }

    float NextNoiseSample(std::uint32_t& state)
    {
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        return static_cast<float>((state >> 8) & UINT32_C(0xFFFF)) /
            32767.5f - 1.0f;
    }

    bool TestInitializationAndReset()
    {
        RnNoiseSuppressor suppressor;

        if (suppressor.IsInitialized() || !suppressor.Initialize())
        {
            return false;
        }

        const bool initialized = suppressor.IsInitialized();
        suppressor.Reset();

        return initialized &&
            !suppressor.IsInitialized() &&
            suppressor.GetLastVadProbability() == 0.0f;
    }

    bool TestRejectsWrongFrameSizes()
    {
        RnNoiseSuppressor suppressor;
        std::array<float, RnNoiseSuppressor::SamplesPerFrame> input{};
        std::array<float, RnNoiseSuppressor::SamplesPerFrame> output{};

        if (!suppressor.Initialize())
        {
            return false;
        }

        return !suppressor.ProcessFrame(
                std::span<const float>(input).first(input.size() - 1),
                output
            ) &&
            !suppressor.ProcessFrame(
                input,
                std::span<float>(output).first(output.size() - 1)
            );
    }

    bool TestProcessesNormalizedAudio()
    {
        RnNoiseSuppressor suppressor;
        std::array<float, RnNoiseSuppressor::SamplesPerFrame> input{};
        std::array<float, RnNoiseSuppressor::SamplesPerFrame> output{};
        std::uint32_t randomState = UINT32_C(0x12345678);
        double phase = 0.0;

        if (!suppressor.Initialize())
        {
            return false;
        }

        for (int frame = 0; frame < 100; ++frame)
        {
            for (std::size_t sample = 0; sample < input.size(); ++sample)
            {
                const float speechLike = static_cast<float>(
                    0.08 * std::sin(phase)
                );
                const float noise = 0.03f * NextNoiseSample(randomState);
                input[sample] = speechLike + noise;
                phase += 2.0 * std::numbers::pi * 220.0 / 48000.0;

                if (phase >= 2.0 * std::numbers::pi)
                {
                    phase -= 2.0 * std::numbers::pi;
                }
            }

            if (!suppressor.ProcessFrame(input, output) ||
                !IsFiniteFrame(output))
            {
                return false;
            }
        }

        const float vad = suppressor.GetLastVadProbability();
        return std::isfinite(vad) && vad >= 0.0f && vad <= 1.0f;
    }

    bool TestSanitizesInvalidInput()
    {
        RnNoiseSuppressor suppressor;
        std::array<float, RnNoiseSuppressor::SamplesPerFrame> input{};
        std::array<float, RnNoiseSuppressor::SamplesPerFrame> output{};

        if (!suppressor.Initialize())
        {
            return false;
        }

        input[0] = std::numeric_limits<float>::quiet_NaN();
        input[1] = std::numeric_limits<float>::infinity();
        input[2] = 2.0f;
        input[3] = -2.0f;

        return suppressor.ProcessFrame(input, output) &&
            IsFiniteFrame(output);
    }
}

int main()
{
    struct NamedTest
    {
        const char* name;
        bool (*test)();
    };

    const NamedTest tests[]{
        {"Initialization and reset", &TestInitializationAndReset},
        {"Rejects wrong frame sizes", &TestRejectsWrongFrameSizes},
        {"Processes normalized audio", &TestProcessesNormalizedAudio},
        {"Sanitizes invalid input", &TestSanitizesInvalidInput}
    };

    bool success = true;

    for (const NamedTest& test : tests)
    {
        const bool passed = test.test();
        std::cout << (passed ? "PASS: " : "FAIL: ")
            << test.name << '\n';
        success = success && passed;
    }

    return success ? 0 : 1;
}
