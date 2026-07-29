#include "audio/VoiceEffectsRuntime.hpp"

#include <thread>

VoiceEffectsRuntime::~VoiceEffectsRuntime()
{
    Shutdown();
}

bool VoiceEffectsRuntime::Initialize() noexcept
{
    if (initialized_.load(std::memory_order_acquire))
    {
        return false;
    }

    pendingSettings_.fill({});
    writeSequence_.store(0, std::memory_order_relaxed);
    readSequence_.store(0, std::memory_order_relaxed);
    rejectedUpdateCount_.store(0, std::memory_order_relaxed);
    activeSubmitCount_.store(0, std::memory_order_relaxed);
    acceptingUpdates_.store(true, std::memory_order_release);
    initialized_.store(true, std::memory_order_release);
    return true;
}

bool VoiceEffectsRuntime::SubmitSettings(
    const VoiceEffectSettings& settings
) noexcept
{
    if (!IsValidVoiceEffectSettings(settings) ||
        !acceptingUpdates_.load(std::memory_order_acquire))
    {
        return false;
    }

    activeSubmitCount_.fetch_add(1, std::memory_order_acq_rel);

    if (!acceptingUpdates_.load(std::memory_order_acquire))
    {
        activeSubmitCount_.fetch_sub(1, std::memory_order_release);
        return false;
    }

    const std::uint64_t writeSequence =
        writeSequence_.load(std::memory_order_relaxed);
    const std::uint64_t readSequence =
        readSequence_.load(std::memory_order_acquire);

    if (writeSequence - readSequence >= PendingUpdateCapacity)
    {
        rejectedUpdateCount_.fetch_add(1, std::memory_order_relaxed);
        activeSubmitCount_.fetch_sub(1, std::memory_order_release);
        return false;
    }

    pendingSettings_[
        static_cast<std::size_t>(writeSequence % PendingUpdateCapacity)
    ] = settings;
    writeSequence_.store(writeSequence + 1, std::memory_order_release);
    activeSubmitCount_.fetch_sub(1, std::memory_order_release);
    return true;
}

bool VoiceEffectsRuntime::ConsumeLatestSettings(
    VoiceEffectSettings& settings
) noexcept
{
    if (!initialized_.load(std::memory_order_acquire))
    {
        return false;
    }

    const std::uint64_t readSequence =
        readSequence_.load(std::memory_order_relaxed);
    const std::uint64_t writeSequence =
        writeSequence_.load(std::memory_order_acquire);

    if (readSequence == writeSequence)
    {
        return false;
    }

    const std::uint64_t newestSequence = writeSequence - 1;
    settings = pendingSettings_[
        static_cast<std::size_t>(newestSequence % PendingUpdateCapacity)
    ];

    // Release every snapshot up to and including the newest one. The producer
    // may reuse those slots only after this release becomes visible.
    readSequence_.store(writeSequence, std::memory_order_release);
    return true;
}

std::uint64_t VoiceEffectsRuntime::RejectedUpdateCount() const noexcept
{
    return rejectedUpdateCount_.load(std::memory_order_relaxed);
}

bool VoiceEffectsRuntime::IsInitialized() const noexcept
{
    return initialized_.load(std::memory_order_acquire);
}

void VoiceEffectsRuntime::Shutdown() noexcept
{
    acceptingUpdates_.store(false, std::memory_order_release);

    while (activeSubmitCount_.load(std::memory_order_acquire) != 0)
    {
        std::this_thread::yield();
    }

    pendingSettings_.fill({});
    writeSequence_.store(0, std::memory_order_relaxed);
    readSequence_.store(0, std::memory_order_relaxed);
    rejectedUpdateCount_.store(0, std::memory_order_relaxed);
    initialized_.store(false, std::memory_order_release);
}
