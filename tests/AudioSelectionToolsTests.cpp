#include "editor/AudioSelectionTools.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

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

    AudioDocument RequireDocument(
        std::optional<AudioDocument> document,
        const std::string_view message
    )
    {
        if (!document.has_value())
        {
            Expect(false, message);
            std::exit(1);
        }
        return std::move(*document);
    }

    void TestFormattingAndParsing()
    {
        Expect(
            FormatAudioFrameTime(0U, 48000U) == "00:00.000",
            "zero frame formats consistently"
        );
        Expect(
            FormatAudioFrameTime(90000U, 48000U) == "00:01.875",
            "frame time includes milliseconds"
        );
        Expect(
            FormatAudioFrameTime(177120000U, 48000U) == "1:01:30.000",
            "hour format is supported"
        );

        const auto seconds = ParseAudioFrameTime("1.250", 48000U, 96000U);
        Expect(seconds.has_value() && seconds->frame == 60000U,
            "plain seconds parse");

        const auto minuteTime = ParseAudioFrameTime("01:02,500", 48000U, 4000000U);
        Expect(minuteTime.has_value() && minuteTime->frame == 3000000U,
            "minute time accepts comma decimal separator");

        const auto hourTime = ParseAudioFrameTime("1:02:03.125", 48000U, 200000000U);
        Expect(hourTime.has_value() && hourTime->frame == 178710000U,
            "hour time parses");

        const auto clamped = ParseAudioFrameTime("99:00", 1000U, 5000U);
        Expect(clamped.has_value() && clamped->frame == 5000U && clamped->clamped,
            "times beyond the document clamp safely");

        Expect(!ParseAudioFrameTime("1:75", 48000U, 100000U).has_value(),
            "invalid seconds are rejected");
        Expect(!ParseAudioFrameTime("x", 48000U, 100000U).has_value(),
            "non-numeric time is rejected");
        Expect(!ParseAudioFrameTime("", 48000U, 100000U).has_value(),
            "empty time is rejected");
    }

    void TestZeroCrossingSnap()
    {
        std::string errorMessage;
        AudioDocument document = RequireDocument(
            AudioDocument::Create(
                1000U,
                1U,
                {0.8f, 0.6f, 0.2f, -0.1f, -0.7f, -0.4f, 0.3f, 0.9f},
                errorMessage
            ),
            "zero crossing fixture is valid"
        );

        Expect(
            SnapAudioFrameToZeroCrossing(document, 2U, 3U) == 3U,
            "nearest crossing is selected"
        );
        Expect(
            SnapAudioFrameToZeroCrossing(document, 6U, 2U) == 6U,
            "exact crossing boundary is retained"
        );

        const AudioFrameRange snapped = SnapAudioRangeToZeroCrossings(
            document,
            {2U, 5U},
            2U
        );
        Expect(snapped.beginFrame == 3U && snapped.endFrame == 6U,
            "both selection boundaries snap independently");
    }
}

int main()
{
    TestFormattingAndParsing();
    TestZeroCrossingSnap();

    if (failureCount != 0)
    {
        std::cerr << failureCount << " test assertion(s) failed.\n";
        return EXIT_FAILURE;
    }

    std::cout << "AudioSelectionTools tests passed.\n";
    return EXIT_SUCCESS;
}
