#include "editor/AudioEditHistory.hpp"

#include <limits>
#include <new>
#include <utility>

AudioEditHistory::AudioEditHistory(
    const std::size_t maximumEntries,
    const std::size_t maximumBytes
) noexcept
    : maximumEntries_(maximumEntries),
      maximumBytes_(maximumBytes)
{
}

void AudioEditHistory::Clear() noexcept
{
    undoEntries_.clear();
    redoEntries_.clear();
    memoryBytes_ = 0U;
}

bool AudioEditHistory::CanStore(
    const AudioDocument& document
) const noexcept
{
    if (maximumEntries_ == 0U || maximumBytes_ == 0U)
    {
        return false;
    }

    return EstimateBytes(document) <= maximumBytes_;
}

bool AudioEditHistory::Record(
    AudioDocument document,
    const std::uint64_t stateIdentifier
)
{
    const std::size_t bytes = EstimateBytes(document);
    if (maximumEntries_ == 0U || bytes > maximumBytes_)
    {
        return false;
    }

    ClearRedo();
    try
    {
        undoEntries_.push_back(Entry{
            std::move(document),
            stateIdentifier,
            bytes
        });
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }

    memoryBytes_ += bytes;
    TrimToLimits();
    return !undoEntries_.empty();
}

bool AudioEditHistory::Undo(
    AudioDocument& currentDocument,
    std::uint64_t& currentStateIdentifier
)
{
    if (undoEntries_.empty())
    {
        return false;
    }

    Entry restored = std::move(undoEntries_.back());
    undoEntries_.pop_back();
    memoryBytes_ -= restored.bytes;

    const std::size_t currentBytes = EstimateBytes(currentDocument);
    if (maximumEntries_ != 0U && currentBytes <= maximumBytes_)
    {
        try
        {
            redoEntries_.push_back(Entry{
                std::move(currentDocument),
                currentStateIdentifier,
                currentBytes
            });
            memoryBytes_ += currentBytes;
        }
        catch (const std::bad_alloc&)
        {
            for (const Entry& entry : redoEntries_)
            {
                memoryBytes_ -= entry.bytes;
            }
            redoEntries_.clear();
        }
    }
    else
    {
        for (const Entry& entry : redoEntries_)
        {
            memoryBytes_ -= entry.bytes;
        }
        redoEntries_.clear();
    }

    currentDocument = std::move(restored.document);
    currentStateIdentifier = restored.stateIdentifier;
    TrimToLimits();
    return true;
}

bool AudioEditHistory::Redo(
    AudioDocument& currentDocument,
    std::uint64_t& currentStateIdentifier
)
{
    if (redoEntries_.empty())
    {
        return false;
    }

    Entry restored = std::move(redoEntries_.back());
    redoEntries_.pop_back();
    memoryBytes_ -= restored.bytes;

    const std::size_t currentBytes = EstimateBytes(currentDocument);
    if (maximumEntries_ != 0U && currentBytes <= maximumBytes_)
    {
        try
        {
            undoEntries_.push_back(Entry{
                std::move(currentDocument),
                currentStateIdentifier,
                currentBytes
            });
            memoryBytes_ += currentBytes;
        }
        catch (const std::bad_alloc&)
        {
            for (const Entry& entry : undoEntries_)
            {
                memoryBytes_ -= entry.bytes;
            }
            undoEntries_.clear();
        }
    }
    else
    {
        for (const Entry& entry : undoEntries_)
        {
            memoryBytes_ -= entry.bytes;
        }
        undoEntries_.clear();
    }

    currentDocument = std::move(restored.document);
    currentStateIdentifier = restored.stateIdentifier;
    TrimToLimits();
    return true;
}

bool AudioEditHistory::CanUndo() const noexcept
{
    return !undoEntries_.empty();
}

bool AudioEditHistory::CanRedo() const noexcept
{
    return !redoEntries_.empty();
}

std::size_t AudioEditHistory::UndoCount() const noexcept
{
    return undoEntries_.size();
}

std::size_t AudioEditHistory::MemoryBytes() const noexcept
{
    return memoryBytes_;
}

std::size_t AudioEditHistory::EstimateBytes(
    const AudioDocument& document
) noexcept
{
    const std::size_t sampleCount = document.Samples().size();
    if (sampleCount >
        std::numeric_limits<std::size_t>::max() / sizeof(float))
    {
        return std::numeric_limits<std::size_t>::max();
    }

    return sampleCount * sizeof(float);
}

void AudioEditHistory::ClearRedo() noexcept
{
    for (const Entry& entry : redoEntries_)
    {
        memoryBytes_ -= entry.bytes;
    }
    redoEntries_.clear();
}

void AudioEditHistory::TrimToLimits() noexcept
{
    while (undoEntries_.size() > maximumEntries_)
    {
        memoryBytes_ -= undoEntries_.front().bytes;
        undoEntries_.pop_front();
    }

    while (redoEntries_.size() > maximumEntries_)
    {
        memoryBytes_ -= redoEntries_.front().bytes;
        redoEntries_.pop_front();
    }

    while (memoryBytes_ > maximumBytes_)
    {
        if (!undoEntries_.empty())
        {
            memoryBytes_ -= undoEntries_.front().bytes;
            undoEntries_.pop_front();
            continue;
        }

        if (!redoEntries_.empty())
        {
            memoryBytes_ -= redoEntries_.front().bytes;
            redoEntries_.pop_front();
            continue;
        }

        memoryBytes_ = 0U;
        break;
    }
}
