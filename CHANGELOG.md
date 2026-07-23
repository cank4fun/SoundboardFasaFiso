# Changelog

## 2.1.0-rc.1 - Unreleased

- Added optional WebRTC AEC3 acoustic echo cancellation with a soundboard monitor-reference path.
- Added high-pass filtering, RNNoise suppression, automatic gain control, compression and limiting.
- Added raw/processed microphone meters, AEC telemetry, presets and temporary processed-microphone monitoring.
- Added a pinned vcpkg manifest feature and production AEC build path.
- Added generated license notices for WebRTC and its transitive static dependencies.
- Kept a dependency-free build with AEC unavailable when WebRTC is disabled.

## 2.0.0

- Published the first stable v2 release after the `2.0.0-rc.2` validation cycle.
- Added the native single-window control panel with light/dark themes, live meters, device selection, hotkey capture and sound-binding editing.
- Added independent main and monitor outputs, physical microphone routing, automatic audio-device recovery and safe live configuration rollback.
- Added optional startup update checks, per-user Windows startup registration, persistent session logs and console-free launch.
- Added versioned portable Windows archives, package validation and attached SHA-256 checksum files.
- Documented that official GitHub artifacts are currently unsigned and may trigger SmartScreen or Smart App Control.
- Kept the application portable, dependency-free at runtime and compatible with existing v1 configuration keys.

## 2.0.0-rc.2

- Allow startup when no usable sound or hotkey binding exists.
- Keep the GUI available when configured sound files are missing so bindings can be repaired from the Hotkeys tab.

## 2.0.0-rc.1

- Marked the v2 feature set as release-candidate complete
- Changed the portable first-run config to use the Windows default output with monitoring disabled
- Stopped rebuilds from overwriting an existing build-directory config
- Added versioned Windows x64 portable archives and SHA-256 checksum files
- Added release-package validation for required files, supported sample formats and unexpected top-level content
- Marked prerelease tags correctly when publishing GitHub Releases
- Added MSVC control-flow protection, SDL checks and Release linker optimization
- Reduced hidden-window and inactive-tab meter work
- Improved high-DPI window sizing on constrained monitor work areas
- Removed the unused hidden console-startup control while keeping config compatibility
- Reduced absolute path exposure in normal session logs
- Included the MIT license in portable packages
- Isolated vendored miniaudio compilation so project warning and SDL settings no longer produce command-line override warnings
- Corrected the Windows workflow to use the supported `actions/checkout@v6` release
- Validated update links before offering to open them and rejected malformed release versions
- Marked prerelease executables with the Windows prerelease version flag
- Documented unsigned-build, checksum and Smart App Control behavior

## 2.0.0-beta.3

- Added per-monitor DPI awareness and live DPI changes when moving between monitors
- Scaled fonts, controls, margins and minimum window sizing from logical coordinates
- Improved responsive sizing for the theme toggle and sound-binding editor
- Changed binding actions to a two-row layout so translated labels remain readable

## 2.0.0-beta.2

- Added keyboard navigation with `Tab` and `Shift+Tab`
- Added `Ctrl+1`, `Ctrl+2`, `Ctrl+3` tab switching and `Ctrl+S` save/apply
- Added `Esc` cancellation during hotkey capture
- Kept global soundboard hotkeys ahead of local panel shortcuts

## 2.0.0-beta.1

- Reworked the control panel into one window with Main, Settings and Hotkeys tabs
- Kept pending edits intact while changing tabs
- Moved daily controls, device settings and control hotkeys into clearer sections

## 2.0.0-alpha.10

- Switched the executable to the Windows GUI subsystem
- Kept the diagnostic console available on demand
- Fixed single-click window closing so the panel hides to the tray immediately

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
