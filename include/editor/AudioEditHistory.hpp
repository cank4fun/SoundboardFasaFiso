#pragma once

#include "editor/AudioDocument.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>

class AudioEditHistory final
{
public:
    static constexpr std::size_t DefaultMaximumEntries = 16U;
    static constexpr std::size_t DefaultMaximumBytes =
        256U * 1024U * 1024U;

    explicit AudioEditHistory(
        std::size_t maximumEntries = DefaultMaximumEntries,
        std::size_t maximumBytes = DefaultMaximumBytes
    ) noexcept;

    void Clear() noexcept;

    [[nodiscard]] bool CanStore(
        const AudioDocument& document
    ) const noexcept;

    bool Record(
        AudioDocument document,
        std::uint64_t stateIdentifier
    );

    bool Undo(
        AudioDocument& currentDocument,
        std::uint64_t& currentStateIdentifier
    );

    bool Redo(
        AudioDocument& currentDocument,
        std::uint64_t& currentStateIdentifier
    );

    [[nodiscard]] bool CanUndo() const noexcept;
    [[nodiscard]] bool CanRedo() const noexcept;
    [[nodiscard]] std::size_t UndoCount() const noexcept;
    [[nodiscard]] std::size_t MemoryBytes() const noexcept;

private:
    struct Entry final
    {
        AudioDocument document;
        std::uint64_t stateIdentifier = 0U;
        std::size_t bytes = 0U;
    };

    static std::size_t EstimateBytes(
        const AudioDocument& document
    ) noexcept;

    void ClearRedo() noexcept;
    void TrimToLimits() noexcept;

    std::size_t maximumEntries_ = DefaultMaximumEntries;
    std::size_t maximumBytes_ = DefaultMaximumBytes;
    std::size_t memoryBytes_ = 0U;
    std::deque<Entry> undoEntries_;
    std::deque<Entry> redoEntries_;
};
