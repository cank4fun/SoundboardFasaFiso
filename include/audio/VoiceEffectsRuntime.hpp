#pragma once

#include "audio/VoiceEffectSettings.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

// Lock-free single-producer/single-consumer settings bridge. The UI/main
// thread submits complete settings snapshots while the existing microphone
// worker consumes the newest pending snapshot at a 10 ms block boundary.
// This class never creates a thread and performs no heap allocation.
class VoiceEffectsRuntime
{
public:
    static constexpr std::size_t PendingUpdateCapacity = 16;

    VoiceEffectsRuntime() = default;
    ~VoiceEffectsRuntime();

    VoiceEffectsRuntime(const VoiceEffectsRuntime&) = delete;
    VoiceEffectsRuntime& operator=(const VoiceEffectsRuntime&) = delete;

    VoiceEffectsRuntime(VoiceEffectsRuntime&&) = delete;
    VoiceEffectsRuntime& operator=(VoiceEffectsRuntime&&) = delete;

    bool Initialize() noexcept;

    // Single producer: UI/main thread.
    bool SubmitSettings(const VoiceEffectSettings& settings) noexcept;

    // Single consumer: microphone processing worker. If several updates are
    // pending, intermediate snapshots are coalesced and the newest is returned.
    bool ConsumeLatestSettings(VoiceEffectSettings& settings) noexcept;

    [[nodiscard]] std::uint64_t RejectedUpdateCount() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

    void Shutdown() noexcept;

private:
    std::array<VoiceEffectSettings, PendingUpdateCapacity> pendingSettings_{};
    std::atomic<std::uint64_t> writeSequence_{0};
    std::atomic<std::uint64_t> readSequence_{0};
    std::atomic<std::uint64_t> rejectedUpdateCount_{0};
    std::atomic<std::uint32_t> activeSubmitCount_{0};
    std::atomic_bool acceptingUpdates_{false};
    std::atomic_bool initialized_{false};
};
