#include "editor/AudioEditHistory.hpp"

#include <cstdlib>
#include <iostream>
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

    AudioDocument MakeDocument(std::vector<float> samples)
    {
        std::string errorMessage;
        auto document = AudioDocument::Create(
            48000U,
            1U,
            std::move(samples),
            errorMessage
        );

        if (!document.has_value())
        {
            std::cerr << "Document creation failed: " << errorMessage << '\n';
            std::exit(2);
        }

        return std::move(*document);
    }

    void TestUndoAndRedoRestoreDocumentAndState()
    {
        AudioEditHistory history;
        AudioDocument document = MakeDocument({0.1f, 0.2f, 0.3f, 0.4f});
        std::uint64_t stateIdentifier = 10U;

        AudioDocument beforeEdit = document;
        Expect(
            document.Delete(AudioFrameRange{1U, 3U}) ==
                AudioEditResult::Applied,
            "delete applies before recording history"
        );
        Expect(
            history.Record(std::move(beforeEdit), stateIdentifier),
            "pre-edit snapshot is recorded"
        );
        stateIdentifier = 11U;

        Expect(history.CanUndo(), "undo becomes available");
        Expect(!history.CanRedo(), "redo starts unavailable");
        Expect(history.Undo(document, stateIdentifier), "undo succeeds");
        Expect(stateIdentifier == 10U, "undo restores state identifier");
        Expect(document.FrameCount() == 4U, "undo restores frame count");
        Expect(document.Samples()[1] == 0.2f, "undo restores samples");
        Expect(history.CanRedo(), "redo becomes available after undo");

        Expect(history.Redo(document, stateIdentifier), "redo succeeds");
        Expect(stateIdentifier == 11U, "redo restores edited state identifier");
        Expect(document.FrameCount() == 2U, "redo restores edited frame count");
        Expect(document.Samples()[1] == 0.4f, "redo restores edited samples");
    }

    void TestNewEditClearsRedo()
    {
        AudioEditHistory history;
        AudioDocument document = MakeDocument({0.1f, 0.2f, 0.3f});
        std::uint64_t stateIdentifier = 1U;

        AudioDocument firstBefore = document;
        document.Delete(AudioFrameRange{0U, 1U});
        history.Record(std::move(firstBefore), stateIdentifier);
        stateIdentifier = 2U;
        history.Undo(document, stateIdentifier);
        Expect(history.CanRedo(), "redo exists after undo");

        AudioDocument secondBefore = document;
        document.CropTo(AudioFrameRange{0U, 2U});
        Expect(
            history.Record(std::move(secondBefore), stateIdentifier),
            "new branch records successfully"
        );
        Expect(!history.CanRedo(), "new edit clears redo branch");
    }

    void TestLimitsAreEnforced()
    {
        AudioEditHistory entryLimited(2U, 1024U);
        AudioDocument document = MakeDocument({0.1f, 0.2f});

        Expect(entryLimited.Record(document, 1U), "first entry is recorded");
        Expect(entryLimited.Record(document, 2U), "second entry is recorded");
        Expect(entryLimited.Record(document, 3U), "third entry is accepted");
        Expect(
            entryLimited.UndoCount() == 2U,
            "oldest entry is trimmed by count"
        );

        AudioEditHistory byteLimited(4U, sizeof(float));
        Expect(
            !byteLimited.CanStore(document),
            "oversized document is rejected before copying"
        );
        Expect(
            !byteLimited.Record(std::move(document), 1U),
            "oversized snapshot is not recorded"
        );
    }

    void TestClearResetsState()
    {
        AudioEditHistory history;
        AudioDocument document = MakeDocument({0.1f, 0.2f});
        history.Record(document, 1U);
        Expect(history.MemoryBytes() != 0U, "history reports memory use");

        history.Clear();
        Expect(!history.CanUndo(), "clear removes undo entries");
        Expect(!history.CanRedo(), "clear removes redo entries");
        Expect(history.MemoryBytes() == 0U, "clear resets memory use");
    }
}

int main()
{
    TestUndoAndRedoRestoreDocumentAndState();
    TestNewEditClearsRedo();
    TestLimitsAreEnforced();
    TestClearResetsState();

    if (failureCount != 0)
    {
        std::cerr << failureCount << " history assertion(s) failed.\n";
        return 1;
    }

    std::cout << "AudioEditHistory tests passed.\n";
    return 0;
}
