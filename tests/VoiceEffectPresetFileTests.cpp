#include "audio/VoiceEffectPresetFile.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
    int failureCount = 0;

    void Expect(const bool condition, const char* const message)
    {
        if (!condition)
        {
            ++failureCount;
            std::cerr << "FAILED: " << message << '\n';
        }
    }

    class TemporaryDirectory
    {
    public:
        TemporaryDirectory()
        {
            const auto nonce = std::chrono::steady_clock::now()
                .time_since_epoch().count();
            path_ = std::filesystem::temp_directory_path() /
                ("sbff-voice-preset-file-tests-" + std::to_string(nonce));
            std::filesystem::create_directories(path_);
        }

        ~TemporaryDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        const std::filesystem::path& Path() const noexcept
        {
            return path_;
        }

    private:
        std::filesystem::path path_;
    };

    VoiceEffectUserPreset BuildPreset()
    {
        VoiceEffectUserPreset preset;
        preset.name = "6F Rack Test";
        preset.settings.enabled = false;
        preset.settings.bypassed = false;
        preset.settings.preset = VoiceEffectPreset::Custom;
        preset.settings.pitchSemitones = -3.25f;
        preset.settings.formantSemitones = 1.5f;
        preset.settings.character = 0.62f;
        preset.settings.body = 0.45f;
        preset.settings.drive = 0.18f;
        preset.settings.dryWet = 0.77f;
        preset.settings.outputGainDb = -1.75f;
        preset.settings.parametricEqEnabled = true;
        preset.settings.eqLowGainDb = 2.5f;
        preset.settings.eqLowFrequencyHz = 180.0f;
        preset.settings.eqMidGainDb = -3.5f;
        preset.settings.eqMidFrequencyHz = 2100.0f;
        preset.settings.eqMidQ = 1.7f;
        preset.settings.eqHighGainDb = 1.25f;
        preset.settings.eqHighFrequencyHz = 7900.0f;
        preset.settings.deEsserEnabled = true;
        preset.settings.deEsserAmount = 0.72f;
        preset.settings.gateEnabled = true;
        preset.settings.gateAmount = 0.31f;
        preset.settings.compressorEnabled = true;
        preset.settings.compressorAmount = 0.66f;
        preset.settings.rackOrder = {
            VoiceEffectRackModule::Gate,
            VoiceEffectRackModule::ParametricEq,
            VoiceEffectRackModule::Compressor,
            VoiceEffectRackModule::DeEsser
        };
        return preset;
    }

    void TestRoundTripAndOverwrite()
    {
        TemporaryDirectory directory;
        const VoiceEffectUserPreset expected = BuildPreset();
        const auto path = BuildVoiceEffectPresetFilePath(
            directory.Path(), expected
        );
        std::string error;
        Expect(SaveVoiceEffectPresetFile(path, expected, error),
            "valid preset exports atomically");
        Expect(error.empty(), "successful export leaves no error");

        const auto loaded = LoadVoiceEffectPresetFile(path);
        Expect(loaded.preset.has_value(), "exported preset imports");
        if (loaded.preset.has_value())
        {
            Expect(loaded.preset->name == expected.name,
                "preset name round-trips");
            Expect(loaded.preset->settings.rackOrder ==
                    expected.settings.rackOrder,
                "rack order round-trips");
            Expect(loaded.preset->settings.eqMidFrequencyHz ==
                    expected.settings.eqMidFrequencyHz,
                "parametric settings round-trip");
            Expect(loaded.preset->settings.compressorAmount ==
                    expected.settings.compressorAmount,
                "dynamic settings round-trip");
        }

        VoiceEffectUserPreset changed = expected;
        changed.settings.pitchSemitones = 4.25f;
        Expect(SaveVoiceEffectPresetFile(path, changed, error),
            "existing preset file is transactionally replaced");
        const auto overwritten = LoadVoiceEffectPresetFile(path);
        Expect(overwritten.preset.has_value() &&
                overwritten.preset->settings.pitchSemitones == 4.25f,
            "replacement contains the newest settings");
    }

    void TestChecksumAndStrictParsing()
    {
        TemporaryDirectory directory;
        const VoiceEffectUserPreset preset = BuildPreset();
        const auto path = BuildVoiceEffectPresetFilePath(
            directory.Path(), preset
        );
        std::string error;
        Expect(SaveVoiceEffectPresetFile(path, preset, error),
            "fixture exports for tamper test");

        std::ifstream input(path, std::ios::binary);
        std::string text(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>()
        );
        const std::size_t position = text.find("pitch_semitones=-3.25");
        Expect(position != std::string::npos, "tamper target exists");
        if (position != std::string::npos)
        {
            text[position + 16U] = '4';
        }
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.close();

        const auto tampered = LoadVoiceEffectPresetFile(path);
        Expect(!tampered.preset.has_value(),
            "checksum rejects modified preset content");
        Expect(tampered.errorMessage.find("checksum") != std::string::npos,
            "checksum failure is reported clearly");
    }

    void TestDiscoveryAndFileNameSafety()
    {
        TemporaryDirectory directory;
        VoiceEffectUserPreset preset = BuildPreset();
        preset.name = "TR Vokal: Ağır / Temiz";
        const auto path = BuildVoiceEffectPresetFilePath(
            directory.Path(), preset
        );
        Expect(path.filename().wstring().find(L':') == std::wstring::npos &&
                path.filename().wstring().find(L'/') == std::wstring::npos,
            "export filename excludes unsafe path characters");

        std::string error;
        Expect(SaveVoiceEffectPresetFile(path, preset, error),
            "unicode display name exports with an ASCII-safe filename");
        std::ofstream(directory.Path() / "ignore.txt") << "not a preset";
        const auto files = DiscoverVoiceEffectPresetFiles(directory.Path());
        Expect(files.size() == 1U && files[0] == path,
            "discovery returns only .sbffvoice regular files");
    }
}

int main()
{
    TestRoundTripAndOverwrite();
    TestChecksumAndStrictParsing();
    TestDiscoveryAndFileNameSafety();

    if (failureCount != 0)
    {
        std::cerr << failureCount << " preset-file test(s) failed.\n";
        return 1;
    }

    std::cout << "Voice-effect preset-file tests passed.\n";
    return 0;
}
