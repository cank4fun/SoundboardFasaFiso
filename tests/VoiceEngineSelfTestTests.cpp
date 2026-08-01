#include "audio/VoiceEngineSelfTest.hpp"
#include "audio/VoiceEffectsProcessor.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
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

    void TestSelfTestPassesAndPublishesUsefulMetrics()
    {
        const VoiceEngineSelfTestReport report = RunVoiceEngineSelfTest();

        Expect(report.Passed(), "the deterministic voice self-test passes");
        Expect(report.checkCount == 7U, "all self-test checks run");
        Expect(report.failureCount == 0U, "no self-test check fails");
        Expect(report.failedMask == 0U, "the failure mask is empty");
        Expect(report.completedMask == 0x7FU,
            "the completed mask contains every published check");
        Expect(report.processedBlockCount >= 250U,
            "the self-test exercises a meaningful number of blocks");
        Expect(
            report.latencySamples ==
                VoiceEffectsProcessor::ProcessingLatencySamples,
            "the self-test publishes the fixed DSP latency"
        );
        Expect(std::isfinite(report.inputRms) && report.inputRms > 0.01f,
            "the generated speech probe has measurable energy");
        Expect(std::isfinite(report.outputRms) && report.outputRms > 0.002f,
            "the active engine produces measurable output");
        Expect(std::isfinite(report.outputPeak) && report.outputPeak > 0.01f,
            "the active engine publishes a finite output peak");
        Expect(report.outputPeak <= 4.0001f,
            "the self-test output remains inside the engine guard rail");
        Expect(report.disabledPathMaximumError <= 0.0000001f,
            "the disabled path is sample transparent");
        Expect(report.bypassMaximumError < 0.0025f,
            "the dynamic bypass settles back to the input");
        Expect(report.rackDifferenceRms > 0.00035f,
            "different rack orders produce observably different output");
    }

    void TestSelfTestIsDeterministic()
    {
        const VoiceEngineSelfTestReport first = RunVoiceEngineSelfTest();
        const VoiceEngineSelfTestReport second = RunVoiceEngineSelfTest();

        Expect(first.completedMask == second.completedMask,
            "completed checks are deterministic");
        Expect(first.failedMask == second.failedMask,
            "failed checks are deterministic");
        Expect(first.processedBlockCount == second.processedBlockCount,
            "the processed block count is deterministic");
        Expect(std::abs(first.inputRms - second.inputRms) < 0.000001f,
            "input RMS is deterministic");
        Expect(std::abs(first.outputRms - second.outputRms) < 0.000001f,
            "output RMS is deterministic");
        Expect(std::abs(
            first.rackDifferenceRms - second.rackDifferenceRms
        ) < 0.000001f, "rack response is deterministic");
    }
}

int main()
{
    TestSelfTestPassesAndPublishesUsefulMetrics();
    TestSelfTestIsDeterministic();

    if (failureCount != 0)
    {
        std::cerr << failureCount << " self-test assertion(s) failed\n";
        return 1;
    }

    std::cout << "Voice Engine self-test tests passed\n";
    return 0;
}
