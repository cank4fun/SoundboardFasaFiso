# Changelog

## 2.0.0-alpha.9

- Added a dependency-free update checker backed by WinHTTP and the public GitHub Releases API
- Added optional automatic update checks at startup through `check_updates_on_start`
- Added a manual **Check for updates** action to the native control panel
- Added semantic-version comparison for stable and prerelease builds
- Kept network work off the UI thread so audio playback and the control panel remain responsive
- Opens the official GitHub Release page only after explicit user confirmation
- Does not automatically download, replace or execute application files

## 2.0.0-alpha.8

- Added live microphone peak metering to the native control panel
- Added main-output and monitor-output activity meters beside their volume controls
- Added fast attack and smooth decay so short sounds remain visible without flicker
- Added theme-aware green, amber and red meter ranges
- Kept the meters compact by placing them directly below the existing gain sliders
- Added no new runtime or GUI dependency

## 2.0.0-alpha.7

- Added persistent `theme=light` and `theme=dark` configuration
- Added an instant light/dark theme switch to the control-panel header
- Replaced legacy group boxes and push buttons with custom-rendered cards and flat controls
- Added modern typography, spacing, rounded surfaces, accent actions and danger actions
- Added dark title-bar and rounded-window integration on supported Windows versions
- Added theme-aware custom volume sliders so light mode no longer renders black tracks
- Compacted and centered the responsive layout to prevent controls overlapping when maximized
- Preserved the standalone native Win32 architecture with no GUI runtime dependency
- Kept the final visual-polish pass for the beta/release stage

## 2.0.0-alpha.6

- Added persistent session logging with automatic `latest.log` and `previous.log` rotation
- Added an **Open logs folder** action to the native control panel
- Added optional per-user Windows startup registration without administrator privileges
- Added `start_with_windows` and `show_console_on_start` config settings
- Added GUI controls for startup registration and console visibility at launch
- Applied startup and console changes through the existing safe settings transaction
- Kept the application standalone with no new runtime dependency

## 2.0.0-alpha.5

- Added physical microphone capture through miniaudio/WASAPI
- Added real-time microphone routing to the main output, monitor output, or both
- Added microphone device enumeration and selection in the native control panel
- Added microphone enable, gain and routing controls with safe config rollback
- Added microphone-device recovery to the existing audio reconnection flow
- Kept microphone mixing fully native with no new runtime dependency
- Preserved the driver boundary: voice-chat microphone exposure still uses an optional virtual endpoint

## 2.0.0-alpha.4

- Added a visual sound-binding editor with add, update, remove and clear actions
- Added hotkey capture for supported key combinations
- Added editable control hotkeys for stop, mute, reload and exit commands
- Added WAV/MP3/FLAC file selection with optional portable copy into `sounds`
- Added per-binding mode and volume controls
- Kept all changes staged in the pending config until `Save and apply` succeeds
- Preserved runtime validation and rollback for hotkey conflicts and invalid bindings

## 2.0.0-alpha.3

- Added editable main and monitor device selectors backed by live device enumeration
- Added GUI volume sliders, language selection, sample-rate and buffer controls
- Added safe `Save and apply` flow using a pending config and runtime rollback
- Added atomic config writing with temporary and backup files
- Added a device refresh command without restarting the application
- Kept sound bindings and control hotkeys intact while saving core settings
- Preserved the standalone native Win32 GUI with no external runtime

## 2.0.0-alpha.2

- Added a native Win32 control panel with no GUI framework dependency
- Added live status, audio configuration summary and sound-binding list
- Added GUI buttons for reload, stop, mute, console, config, sounds and exit
- Changed tray double-click to open the control panel
- Added a localized tray command for reopening the control panel
- Preserved console, global hotkeys and portable standalone packaging

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
