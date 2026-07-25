#include "editor/AudioEditorEffects.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

namespace
{
    int failureCount = 0;

    void Expect(const bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failureCount;
        }
    }

    void ExpectNear(
        const std::optional<float>& value,
        const float expected,
        const std::string& message
    )
    {
        Expect(value.has_value(), message + " should parse");
        if (value.has_value())
        {
            Expect(
                std::abs(*value - expected) < 0.0001f,
                message + " should have the expected value"
            );
        }
    }
}

int main()
{
    ExpectNear(ParseAudioEffectGainDecibels("6"), 6.0f, "integer gain");
    ExpectNear(ParseAudioEffectGainDecibels(" -3.5 "), -3.5f, "dot gain");
    ExpectNear(ParseAudioEffectGainDecibels("2,25"), 2.25f, "comma gain");
    ExpectNear(ParseAudioEffectGainDecibels("+0.75"), 0.75f, "signed gain");

    Expect(!ParseAudioEffectGainDecibels("").has_value(), "empty gain should fail");
    Expect(!ParseAudioEffectGainDecibels("1.2.3").has_value(), "multiple dots should fail");
    Expect(!ParseAudioEffectGainDecibels("1,2.3").has_value(), "mixed separators should fail");
    Expect(!ParseAudioEffectGainDecibels("25").has_value(), "gain above maximum should fail");
    Expect(!ParseAudioEffectGainDecibels("-61").has_value(), "gain below minimum should fail");
    Expect(!ParseAudioEffectGainDecibels("nan").has_value(), "non-finite gain should fail");
    Expect(!ParseAudioEffectGainDecibels("4 dB").has_value(), "trailing text should fail");

    const std::optional<AudioFrameRange> selection{AudioFrameRange{10U, 30U}};
    const auto selectionRange = ResolveAudioEffectRange(
        100U,
        selection,
        AudioEffectScope::Selection
    );
    Expect(selectionRange.has_value(), "selection scope should resolve");
    if (selectionRange.has_value())
    {
        Expect(selectionRange->beginFrame == 10U, "selection begin should be preserved");
        Expect(selectionRange->endFrame == 30U, "selection end should be preserved");
    }

    const auto wholeRange = ResolveAudioEffectRange(
        100U,
        selection,
        AudioEffectScope::WholeDocument
    );
    Expect(wholeRange.has_value(), "whole-document scope should resolve");
    if (wholeRange.has_value())
    {
        Expect(wholeRange->beginFrame == 0U, "whole-document begin should be zero");
        Expect(wholeRange->endFrame == 100U, "whole-document end should match frame count");
    }

    const auto fallbackRange = ResolveAudioEffectRange(
        100U,
        std::nullopt,
        AudioEffectScope::Selection
    );
    Expect(fallbackRange.has_value(), "missing selection should fall back to whole document");
    if (fallbackRange.has_value())
    {
        Expect(fallbackRange->beginFrame == 0U, "fallback begin should be zero");
        Expect(fallbackRange->endFrame == 100U, "fallback end should match frame count");
    }

    Expect(
        !ResolveAudioEffectRange(
            0U,
            std::nullopt,
            AudioEffectScope::WholeDocument
        ).has_value(),
        "empty documents should not resolve an effect range"
    );

    std::string documentError;
    auto document = AudioDocument::Create(
        48000U,
        1U,
        {0.25f, -0.5f, 0.1f},
        documentError
    );
    Expect(document.has_value(), "gain clipping test document should be created");
    if (document.has_value())
    {
        Expect(
            !WouldAudioEffectGainExceedUnitPeak(
                *document,
                AudioFrameRange{0U, 3U},
                6.0f
            ),
            "six decibels should keep a 0.5 peak at or below unit peak"
        );
        Expect(
            WouldAudioEffectGainExceedUnitPeak(
                *document,
                AudioFrameRange{0U, 3U},
                7.0f
            ),
            "seven decibels should exceed unit peak"
        );
        Expect(
            !WouldAudioEffectGainExceedUnitPeak(
                *document,
                AudioFrameRange{0U, 1U},
                7.0f
            ),
            "range-limited clipping checks should ignore samples outside the range"
        );
    }

    if (failureCount != 0)
    {
        std::cerr << failureCount << " test assertion(s) failed.\n";
        return EXIT_FAILURE;
    }

    std::cout << "AudioEditorEffects tests passed.\n";
    return EXIT_SUCCESS;
}
