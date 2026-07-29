#include "audio/VoiceEffectsRuntime.hpp"

#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>
#include <thread>

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

    VoiceEffectSettings CustomSettings(const float character)
    {
        VoiceEffectSettings settings;
        settings.enabled = true;
        settings.preset = VoiceEffectPreset::Custom;
        settings.pitchSemitones = 1.0f;
        settings.formantSemitones = -0.5f;
        settings.character = character;
        settings.drive = 0.25f;
        settings.dryWet = 0.75f;
        settings.outputGainDb = -1.0f;
        return settings;
    }

    void TestLifecycleAndValidation()
    {
        VoiceEffectsRuntime runtime;
        VoiceEffectSettings output;

        Expect(!runtime.IsInitialized(), "runtime starts uninitialized");
        Expect(!runtime.SubmitSettings({}),
            "updates are rejected before initialization");
        Expect(runtime.Initialize(), "runtime initializes");
        Expect(runtime.IsInitialized(), "initialized state is reported");
        Expect(!runtime.Initialize(), "double initialization is rejected");
        Expect(!runtime.ConsumeLatestSettings(output),
            "a new runtime has no pending update");

        VoiceEffectSettings invalid;
        invalid.pitchSemitones =
            std::numeric_limits<float>::quiet_NaN();
        Expect(!runtime.SubmitSettings(invalid),
            "invalid settings are rejected before publication");

        runtime.Shutdown();
        Expect(!runtime.IsInitialized(), "shutdown clears initialized state");
        Expect(!runtime.SubmitSettings({}),
            "updates are rejected after shutdown");
    }

    void TestLatestPendingSettingsWin()
    {
        VoiceEffectsRuntime runtime;
        Expect(runtime.Initialize(), "coalescing runtime initializes");

        const VoiceEffectSettings first = CustomSettings(0.1f);
        const VoiceEffectSettings second = CustomSettings(0.5f);
        const VoiceEffectSettings third = CustomSettings(0.9f);

        Expect(runtime.SubmitSettings(first), "first update is accepted");
        Expect(runtime.SubmitSettings(second), "second update is accepted");
        Expect(runtime.SubmitSettings(third), "third update is accepted");

        VoiceEffectSettings consumed;
        Expect(runtime.ConsumeLatestSettings(consumed),
            "the newest pending update is consumed");
        Expect(consumed.preset == VoiceEffectPreset::Custom,
            "the consumed preset is preserved");
        Expect(std::abs(consumed.character - third.character) < 0.000001f,
            "intermediate slider updates are coalesced");
        Expect(!runtime.ConsumeLatestSettings(consumed),
            "coalescing drains all older pending updates");
    }

    void TestFullQueueRejectsWithoutOverwriting()
    {
        VoiceEffectsRuntime runtime;
        Expect(runtime.Initialize(), "capacity runtime initializes");

        for (std::size_t index = 0;
            index < VoiceEffectsRuntime::PendingUpdateCapacity;
            ++index)
        {
            const float character = static_cast<float>(index) /
                static_cast<float>(
                    VoiceEffectsRuntime::PendingUpdateCapacity
                );
            Expect(runtime.SubmitSettings(CustomSettings(character)),
                "a free queue slot accepts an update");
        }

        Expect(!runtime.SubmitSettings(CustomSettings(1.0f)),
            "a full queue rejects rather than overwriting unread data");
        Expect(runtime.RejectedUpdateCount() == 1,
            "queue overflow is observable");

        VoiceEffectSettings consumed;
        Expect(runtime.ConsumeLatestSettings(consumed),
            "the newest accepted update remains consumable");
        const float expected = static_cast<float>(
            VoiceEffectsRuntime::PendingUpdateCapacity - 1
        ) / static_cast<float>(VoiceEffectsRuntime::PendingUpdateCapacity);
        Expect(std::abs(consumed.character - expected) < 0.000001f,
            "a rejected update does not replace the newest accepted one");
    }

    void TestConcurrentProducerAndConsumer()
    {
        VoiceEffectsRuntime runtime;
        Expect(runtime.Initialize(), "concurrent runtime initializes");

        constexpr int UpdateCount = 20000;
        std::atomic_bool producerFinished{false};
        std::atomic<int> acceptedUpdates{0};
        float lastCharacter = 0.0f;

        std::thread producer([&runtime, &producerFinished, &acceptedUpdates]() {
            for (int index = 1; index <= UpdateCount; ++index)
            {
                const float character = static_cast<float>(index) /
                    static_cast<float>(UpdateCount);
                const VoiceEffectSettings settings =
                    CustomSettings(character);

                while (!runtime.SubmitSettings(settings))
                {
                    std::this_thread::yield();
                }

                acceptedUpdates.fetch_add(1, std::memory_order_relaxed);
            }

            producerFinished.store(true, std::memory_order_release);
        });

        int consumedSnapshots = 0;
        while (!producerFinished.load(std::memory_order_acquire))
        {
            VoiceEffectSettings consumed;
            if (runtime.ConsumeLatestSettings(consumed))
            {
                Expect(IsValidVoiceEffectSettings(consumed),
                    "concurrent consumer receives a complete valid snapshot");
                Expect(consumed.character >= lastCharacter,
                    "concurrent snapshots do not move backwards");
                lastCharacter = consumed.character;
                ++consumedSnapshots;
            }
            else
            {
                std::this_thread::yield();
            }
        }

        producer.join();

        VoiceEffectSettings consumed;
        if (runtime.ConsumeLatestSettings(consumed))
        {
            lastCharacter = consumed.character;
            ++consumedSnapshots;
        }

        Expect(acceptedUpdates.load(std::memory_order_relaxed) == UpdateCount,
            "all retried concurrent updates are accepted");
        Expect(consumedSnapshots > 0,
            "the concurrent consumer observes published updates");
        Expect(std::abs(lastCharacter - 1.0f) < 0.000001f,
            "the final concurrent snapshot reaches the consumer");
    }
}

int main()
{
    TestLifecycleAndValidation();
    TestLatestPendingSettingsWin();
    TestFullQueueRejectsWithoutOverwriting();
    TestConcurrentProducerAndConsumer();

    if (failureCount != 0)
    {
        std::cerr << failureCount << " test assertion(s) failed.\n";
        return 1;
    }

    std::cout << "Voice-effects runtime tests passed.\n";
    return 0;
}
