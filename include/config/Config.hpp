#pragma once

#include "localization/Localization.hpp"
#include "sound/PlaybackMode.hpp"

#include <filesystem>
#include <string>
#include <vector>

enum class AppTheme
{
    Light,
    Dark
};

struct SoundBinding
{
    std::string keyName;
    unsigned int modifiers = 0;
    unsigned int virtualKey = 0;
    std::filesystem::path soundFile;
    float volume = 1.0f;
    PlaybackMode mode = PlaybackMode::Restart;
};

class Config
{
public:
    bool Load(const std::filesystem::path& filePath);
    bool Save(const std::filesystem::path& filePath) const;

    void SetLanguage(Language language);
    void SetTheme(AppTheme theme);
    void SetOutputDevice(std::string deviceName);
    bool SetOutputVolume(float volume);
    void SetMonitorDevice(std::string deviceName);
    bool SetMonitorVolume(float volume);

    void SetMicrophoneEnabled(bool enabled);
    void SetMicrophoneDevice(std::string deviceName);
    bool SetMicrophoneVolume(float volume);
    void SetMicrophoneToOutput(bool enabled);
    void SetMicrophoneToMonitor(bool enabled);

    bool SetAudioSampleRate(unsigned int sampleRate);
    bool SetAudioBufferMilliseconds(unsigned int bufferMilliseconds);

    void SetStartWithWindows(bool enabled);
    void SetShowConsoleOnStart(bool enabled);
    void SetCheckUpdatesOnStart(bool enabled);

    bool SetControlHotkeys(
        std::string stopKeyName,
        std::string outputMuteKeyName,
        std::string monitorMuteKeyName,
        std::string reloadKeyName,
        std::string exitKeyName
    );

    bool SetBindings(std::vector<SoundBinding> bindings);

    Language GetLanguage() const;
    AppTheme GetTheme() const;

    const std::string& GetOutputDevice() const;
    float GetOutputVolume() const;

    const std::string& GetMonitorDevice() const;
    float GetMonitorVolume() const;

    bool GetMicrophoneEnabled() const;
    const std::string& GetMicrophoneDevice() const;
    float GetMicrophoneVolume() const;
    bool GetMicrophoneToOutput() const;
    bool GetMicrophoneToMonitor() const;

    unsigned int GetAudioSampleRate() const;
    unsigned int GetAudioBufferMilliseconds() const;

    bool GetStartWithWindows() const;
    bool GetCheckUpdatesOnStart() const;

    const std::string& GetStopKeyName() const;
    unsigned int GetStopModifiers() const;
    unsigned int GetStopVirtualKey() const;

    const std::string& GetOutputMuteKeyName() const;
    unsigned int GetOutputMuteModifiers() const;
    unsigned int GetOutputMuteVirtualKey() const;

    const std::string& GetMonitorMuteKeyName() const;
    unsigned int GetMonitorMuteModifiers() const;
    unsigned int GetMonitorMuteVirtualKey() const;

    const std::string& GetReloadKeyName() const;
    unsigned int GetReloadModifiers() const;
    unsigned int GetReloadVirtualKey() const;

    const std::string& GetExitKeyName() const;
    unsigned int GetExitModifiers() const;
    unsigned int GetExitVirtualKey() const;

    const std::vector<SoundBinding>& GetBindings() const;

private:
    Language language_ = Language::Turkish;
    AppTheme theme_ = AppTheme::Dark;

    std::string outputDevice_ = "default";
    float outputVolume_ = 1.0f;

    std::string monitorDevice_ = "default";
    float monitorVolume_ = 0.30f;

    bool microphoneEnabled_ = false;
    std::string microphoneDevice_ = "default";
    float microphoneVolume_ = 1.0f;
    bool microphoneToOutput_ = true;
    bool microphoneToMonitor_ = false;

    unsigned int audioSampleRate_ = 48000;
    unsigned int audioBufferMilliseconds_ = 5;

    bool startWithWindows_ = false;
    bool showConsoleOnStart_ = false;
    bool checkUpdatesOnStart_ = true;

    std::string stopKeyName_ = "F11";
    unsigned int stopModifiers_ = 0;
    unsigned int stopVirtualKey_ = 0;

    std::string outputMuteKeyName_ = "CTRL+SHIFT+F9";
    unsigned int outputMuteModifiers_ = 0;
    unsigned int outputMuteVirtualKey_ = 0;

    std::string monitorMuteKeyName_ = "CTRL+SHIFT+F10";
    unsigned int monitorMuteModifiers_ = 0;
    unsigned int monitorMuteVirtualKey_ = 0;

    std::string reloadKeyName_ = "CTRL+SHIFT+F11";
    unsigned int reloadModifiers_ = 0;
    unsigned int reloadVirtualKey_ = 0;

    std::string exitKeyName_ = "CTRL+SHIFT+F12";
    unsigned int exitModifiers_ = 0;
    unsigned int exitVirtualKey_ = 0;

    std::vector<SoundBinding> bindings_;
};
