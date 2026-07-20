# Changelog

## 2.0.0-alpha.1

- Added embedded Turkish and English localization infrastructure
- Added `language=tr` and `language=en` config support
- Localized console, tray, hotkey, audio and config diagnostics
- Added alpha-version metadata and prerelease-tag validation
- Documented the v2 standalone audio architecture and driver boundary
- Preserved compatibility with v1 configuration keys and sound bindings


## 1.0.0

- Global Win32 hotkeys with live config reload
- Separate main and monitor outputs
- WAV, MP3 and FLAC playback from RAM
- Restart, overlap, toggle and loop modes
- Per-sound volume and bounded voice pooling
- Output mute controls and stop-all command
- Automatic recovery after device disconnects
- Configurable WASAPI sample rate and buffer target
- UTF-8 console and Unicode sound-file paths
- Tray menu, custom icon and single-instance protection
- Portable Windows release packaging and automated GitHub releases
