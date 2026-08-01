#pragma once

#include "audio/VoiceEffectSettings.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

inline constexpr wchar_t VoiceEffectPresetFileExtension[] = L".sbffvoice";

struct VoiceEffectPresetFileLoadResult
{
    std::optional<VoiceEffectUserPreset> preset;
    std::string errorMessage;
};

[[nodiscard]] std::filesystem::path BuildVoiceEffectPresetFilePath(
    const std::filesystem::path& folder,
    const VoiceEffectUserPreset& preset
);

[[nodiscard]] bool SaveVoiceEffectPresetFile(
    const std::filesystem::path& path,
    const VoiceEffectUserPreset& preset,
    std::string& errorMessage
);

[[nodiscard]] VoiceEffectPresetFileLoadResult LoadVoiceEffectPresetFile(
    const std::filesystem::path& path
);

[[nodiscard]] std::vector<std::filesystem::path>
    DiscoverVoiceEffectPresetFiles(const std::filesystem::path& folder);
