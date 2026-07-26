#include "editor/AudioDocument.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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

    bool NearlyEqual(
        const float left,
        const float right,
        const float tolerance = 0.00001f
    )
    {
        return std::abs(left - right) <= tolerance;
    }

    void ExpectSamples(
        const std::span<const float> actual,
        const std::span<const float> expected,
        const std::string_view message
    )
    {
        if (actual.size() != expected.size())
        {
            Expect(false, message);
            return;
        }

        for (std::size_t index = 0; index < actual.size(); ++index)
        {
            if (!NearlyEqual(actual[index], expected[index]))
            {
                Expect(false, message);
                return;
            }
        }
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

    AudioDocument CreateStereoDocument()
    {
        std::string errorMessage;
        return RequireDocument(
            AudioDocument::Create(
                48000U,
                2U,
                {
                    0.1f, -0.1f,
                    0.2f, -0.2f,
                    0.3f, -0.3f,
                    0.4f, -0.4f
                },
                errorMessage
            ),
            "stereo fixture is valid"
        );
    }

    void TestCreationValidation()
    {
        std::string errorMessage;

        Expect(
            !AudioDocument::Create(
                0U,
                2U,
                {0.0f, 0.0f},
                errorMessage
            ).has_value(),
            "zero sample rate is rejected"
        );
        Expect(!errorMessage.empty(), "sample-rate error is reported");

        Expect(
            !AudioDocument::Create(
                48000U,
                0U,
                {},
                errorMessage
            ).has_value(),
            "zero channels are rejected"
        );

        Expect(
            !AudioDocument::Create(
                48000U,
                2U,
                {0.0f, 0.0f, 0.0f},
                errorMessage
            ).has_value(),
            "partial interleaved frame is rejected"
        );

        Expect(
            !AudioDocument::Create(
                48000U,
                1U,
                {std::numeric_limits<float>::infinity()},
                errorMessage
            ).has_value(),
            "non-finite samples are rejected"
        );

        Expect(
            AudioDocument::Create(
                48000U,
                1U,
                {1.25f},
                errorMessage
            ).has_value(),
            "finite floating-point headroom is retained"
        );
    }

    void TestMetadataAndRanges()
    {
        AudioDocument document = CreateStereoDocument();

        Expect(document.SampleRate() == 48000U, "sample rate is retained");
        Expect(document.ChannelCount() == 2U, "channel count is retained");
        Expect(document.FrameCount() == 4U, "frame count is derived");
        Expect(
            NearlyEqual(
                static_cast<float>(document.DurationSeconds()),
                4.0f / 48000.0f
            ),
            "duration is frame based"
        );
        Expect(
            document.IsValidRange({1U, 4U}),
            "in-bounds range is valid"
        );
        Expect(
            !document.IsValidRange({3U, 2U}),
            "reversed range is invalid"
        );
        Expect(
            !document.IsValidRange({0U, 5U}),
            "out-of-bounds range is invalid"
        );
    }

    void TestCropAndDelete()
    {
        AudioDocument cropped = CreateStereoDocument();
        Expect(
            cropped.CropTo({1U, 3U}) == AudioEditResult::Applied,
            "crop is applied"
        );
        const std::vector<float> expectedCrop{
            0.2f, -0.2f,
            0.3f, -0.3f
        };
        ExpectSamples(
            cropped.Samples(),
            expectedCrop,
            "crop preserves complete interleaved frames"
        );

        AudioDocument deleted = CreateStereoDocument();
        Expect(
            deleted.Delete({1U, 3U}) == AudioEditResult::Applied,
            "delete is applied"
        );
        const std::vector<float> expectedDelete{
            0.1f, -0.1f,
            0.4f, -0.4f
        };
        ExpectSamples(
            deleted.Samples(),
            expectedDelete,
            "delete removes complete interleaved frames"
        );
        Expect(
            deleted.Delete({1U, 1U}) == AudioEditResult::NoChange,
            "empty delete is a no-op"
        );
    }

    void TestGainAndNormalize()
    {
        std::string errorMessage;
        AudioDocument document = RequireDocument(
            AudioDocument::Create(
                48000U,
                1U,
                {0.25f, -0.5f, 0.75f, 0.0f},
                errorMessage
            ),
            "gain fixture is valid"
        );

        Expect(
            document.ApplyGainDecibels({0U, 3U}, 6.0205999f) ==
                AudioEditResult::Applied,
            "gain is applied to the selected frames"
        );
        const std::vector<float> expectedGain{0.5f, -1.0f, 1.5f, 0.0f};
        ExpectSamples(
            document.Samples(),
            expectedGain,
            "gain preserves floating-point headroom"
        );

        Expect(
            document.NormalizePeak({0U, 4U}, 0.8f) ==
                AudioEditResult::Applied,
            "peak normalization is applied"
        );
        const std::vector<float> expectedNormalize{
            0.26666667f, -0.53333336f, 0.8f, 0.0f
        };
        ExpectSamples(
            document.Samples(),
            expectedNormalize,
            "normalization reaches the requested peak"
        );

        Expect(
            document.NormalizePeak({0U, 4U}, 0.0f) ==
                AudioEditResult::InvalidValue,
            "invalid normalization target is rejected"
        );
        Expect(
            document.ApplyGainDecibels(
                {0U, 4U},
                std::numeric_limits<float>::quiet_NaN()
            ) == AudioEditResult::InvalidValue,
            "non-finite gain is rejected"
        );
    }

    void TestFades()
    {
        std::string errorMessage;
        AudioDocument fadeIn = RequireDocument(
            AudioDocument::Create(
                48000U,
                2U,
                {
                    1.0f, -1.0f,
                    1.0f, -1.0f,
                    1.0f, -1.0f
                },
                errorMessage
            ),
            "fade-in fixture is valid"
        );
        Expect(
            fadeIn.FadeIn({0U, 3U}) == AudioEditResult::Applied,
            "fade-in is applied"
        );
        const std::vector<float> expectedFadeIn{
            0.0f, 0.0f,
            0.5f, -0.5f,
            1.0f, -1.0f
        };
        ExpectSamples(
            fadeIn.Samples(),
            expectedFadeIn,
            "fade-in has exact endpoints"
        );

        AudioDocument fadeOut = RequireDocument(
            AudioDocument::Create(
                48000U,
                2U,
                {
                    1.0f, -1.0f,
                    1.0f, -1.0f,
                    1.0f, -1.0f
                },
                errorMessage
            ),
            "fade-out fixture is valid"
        );
        Expect(
            fadeOut.FadeOut({0U, 3U}) == AudioEditResult::Applied,
            "fade-out is applied"
        );
        const std::vector<float> expectedFadeOut{
            1.0f, -1.0f,
            0.5f, -0.5f,
            0.0f, 0.0f
        };
        ExpectSamples(
            fadeOut.Samples(),
            expectedFadeOut,
            "fade-out has exact endpoints"
        );
    }

    void TestMonoConversion()
    {
        std::string errorMessage;
        AudioDocument document = RequireDocument(
            AudioDocument::Create(
                44100U,
                2U,
                {
                    1.0f, -1.0f,
                    0.5f, 0.25f,
                    -0.5f, -0.25f
                },
                errorMessage
            ),
            "mono fixture is valid"
        );
        Expect(
            document.ConvertToMono() == AudioEditResult::Applied,
            "stereo converts to mono"
        );
        Expect(document.ChannelCount() == 1U, "mono channel count is stored");
        Expect(document.FrameCount() == 3U, "mono keeps frame count");
        const std::vector<float> expected{0.0f, 0.375f, -0.375f};
        ExpectSamples(
            document.Samples(),
            expected,
            "mono conversion averages channels"
        );
        Expect(
            document.ConvertToMono() == AudioEditResult::NoChange,
            "already-mono conversion is a no-op"
        );
    }


    void TestCopyInsertAndSilence()
    {
        AudioDocument source = CreateStereoDocument();
        std::string errorMessage;
        std::optional<AudioDocument> copied = source.CopyRange(
            {1U, 3U},
            errorMessage
        );
        Expect(copied.has_value(), "valid range copies into a document");
        Expect(errorMessage.empty(), "copy range does not report an error");
        if (!copied.has_value())
        {
            return;
        }

        Expect(copied->FrameCount() == 2U, "copied range keeps frame count");
        const std::vector<float> expectedCopied{
            0.2f, -0.2f,
            0.3f, -0.3f
        };
        ExpectSamples(
            copied->Samples(),
            expectedCopied,
            "copied range keeps interleaved samples"
        );

        AudioDocument destination = CreateStereoDocument();
        Expect(
            destination.Insert(1U, *copied) == AudioEditResult::Applied,
            "compatible audio inserts at the requested frame"
        );
        const std::vector<float> expectedInserted{
            0.1f, -0.1f,
            0.2f, -0.2f,
            0.3f, -0.3f,
            0.2f, -0.2f,
            0.3f, -0.3f,
            0.4f, -0.4f
        };
        ExpectSamples(
            destination.Samples(),
            expectedInserted,
            "insert preserves source and destination order"
        );

        Expect(
            destination.Silence({1U, 3U}) == AudioEditResult::Applied,
            "selected frames can be silenced"
        );
        const std::vector<float> expectedSilenced{
            0.1f, -0.1f,
            0.0f, 0.0f,
            0.0f, 0.0f,
            0.2f, -0.2f,
            0.3f, -0.3f,
            0.4f, -0.4f
        };
        ExpectSamples(
            destination.Samples(),
            expectedSilenced,
            "silence clears every channel in the range"
        );
        Expect(
            destination.Silence({1U, 3U}) == AudioEditResult::NoChange,
            "silencing an already-silent range is a no-op"
        );

        AudioDocument mono = RequireDocument(
            AudioDocument::Create(48000U, 1U, {0.25f}, errorMessage),
            "mono insert fixture is valid"
        );
        Expect(
            destination.Insert(0U, mono) == AudioEditResult::InvalidValue,
            "channel-mismatched insert is rejected"
        );
        Expect(
            destination.Insert(destination.FrameCount() + 1U, *copied) ==
                AudioEditResult::InvalidRange,
            "out-of-range insert is rejected"
        );
        Expect(
            !source.CopyRange({2U, 2U}, errorMessage).has_value(),
            "empty copy range is rejected"
        );
    }

    void TestEdgeCases()
    {
        AudioDocument document = CreateStereoDocument();
        Expect(
            document.CropTo({0U, document.FrameCount()}) ==
                AudioEditResult::NoChange,
            "full-range crop is a no-op"
        );

        Expect(
            document.Delete({0U, document.FrameCount()}) ==
                AudioEditResult::Applied,
            "deleting the full range is supported"
        );
        Expect(document.Empty(), "full-range delete leaves an empty document");
        Expect(document.FrameCount() == 0U, "empty document has zero frames");

        std::string errorMessage;
        AudioDocument silence = RequireDocument(
            AudioDocument::Create(
                48000U,
                1U,
                {0.0f, 0.0f},
                errorMessage
            ),
            "silence fixture is valid"
        );
        Expect(
            silence.ApplyGainDecibels({0U, 2U}, 12.0f) ==
                AudioEditResult::NoChange,
            "gain on silence is a no-op"
        );
        Expect(
            silence.NormalizePeak({0U, 2U}) ==
                AudioEditResult::NoChange,
            "silence cannot be peak-normalized"
        );

        AudioDocument singleFrame = RequireDocument(
            AudioDocument::Create(
                48000U,
                1U,
                {0.75f},
                errorMessage
            ),
            "single-frame fixture is valid"
        );
        Expect(
            singleFrame.FadeIn({0U, 1U}) == AudioEditResult::Applied,
            "single-frame fade is applied"
        );
        const std::vector<float> expectedSilence{0.0f};
        ExpectSamples(
            singleFrame.Samples(),
            expectedSilence,
            "single-frame fade reaches silence"
        );
    }

    void TestInvalidRangeDoesNotMutate()
    {
        AudioDocument document = CreateStereoDocument();
        const std::vector<float> before{
            document.Samples().begin(),
            document.Samples().end()
        };

        Expect(
            document.Delete({0U, 5U}) == AudioEditResult::InvalidRange,
            "invalid delete range is rejected"
        );
        Expect(
            document.CropTo({2U, 2U}) == AudioEditResult::InvalidRange,
            "empty crop range is rejected"
        );
        ExpectSamples(
            document.Samples(),
            before,
            "rejected edits leave samples unchanged"
        );
    }
}

int main()
{
    TestCreationValidation();
    TestMetadataAndRanges();
    TestCropAndDelete();
    TestGainAndNormalize();
    TestFades();
    TestMonoConversion();
    TestCopyInsertAndSilence();
    TestEdgeCases();
    TestInvalidRangeDoesNotMutate();

    if (failureCount != 0)
    {
        std::cerr << failureCount << " test assertion(s) failed.\n";
        return 1;
    }

    std::cout << "Audio document tests passed.\n";
    return 0;
}
