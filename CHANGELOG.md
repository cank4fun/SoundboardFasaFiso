# Changelog

## Unreleased

## 2.3.0 - 2026-08-01

- Rebuilt the deterministic local Voice Effects path as Classic Voice Engine 2 without AI inference, model files, network access, a background service or a new runtime dependency.
- Added an allocation-free Speech Analysis Core with 70–700 Hz pitch tracking, voiced/unvoiced confidence, speech activity, onset/transient detection, spectral descriptors and a reusable smoothed spectral envelope.
- Added Pitch Engine 2, combining pitch-synchronous dual-grain processing with the phase-locked spectral path while protecting consonants, attacks and unvoiced speech.
- Added Formant Engine 2 with independent log-domain spectral-envelope warping, temporal/spatial smoothing, energy normalization and bounded resonance correction.
- Replaced the original body stage with Vocal Weight Engine 2, using analysis-guided chest reinforcement and boxiness control without moving pitch or formants.
- Added an allocation-free voice-polish chain with adjustable three-band parametric EQ, split-band de-esser, speech-aware gate/expander and soft-knee compressor.
- Added a modular EQ/de-esser/gate/compressor rack with safe live reordering, independent bypass controls and persistent rack order.
- Added portable, versioned `.sbffvoice` preset import/export with UTF-8 validation, strict size and format checks, checksums and transactional overwrite recovery.
- Added a deterministic inline Voice Engine self-test covering the 48 kHz block contract, fixed 16 ms latency, bypass transparency, finite/bounded output, rack behavior and reset/reinitialization.
- Added compact embedded controls for every new module while preserving the single-window UI and existing user-preset workflow.
- Extended configuration, preset, processor, runtime, robustness and microphone-pipeline tests for the new engines and backward compatibility.
- Preserved the RNNoise-to-AGC Voice Effects position, existing AEC3 and stereo-crosstalk behavior, routing, lock-free settings delivery and portable standalone distribution.
- Synchronized the vcpkg manifest with the public 2.3.0 version and made configuration fail early when release metadata drifts.
- Removed the obsolete WebRTC AEC3 spike compatibility switch; supported builds now use the production AEC3 options exclusively.

## 2.2.1 - 2026-07-30

- Added an independent Body / Vocal Weight control to the embedded Voice Effects UI, configuration format, runtime settings bridge, user presets and automated tests.
- Implemented Vocal Weight as a pitch-free voiced low-band parallel-compression and gentle harmonic-density stage, reinforcing chest weight without the metallic artifacts caused by pitch shifting.
- Kept reinforcement away from the boxy 400–800 Hz region and preserved consonant presence with a voicing- and level-aware gate.
- Preserved existing built-in preset behavior by default; v2.2.0 configurations and user presets without a Body value load safely with Vocal Weight set to zero.
- Kept the fixed 16 ms Voice Effects latency, lock-free real-time path, portable distribution and existing AEC3, crosstalk, RNNoise and routing behavior.

## 2.2.0 - 2026-07-29

- Added a compact Voice Effects / Voice Changer tab inside the existing control panel without introducing popup windows.
- Added independent real-time pitch (`-12` to `+12` semitones) and formant (`-6` to `+6` semitones) controls while preserving speech duration.
- Added a fixed-latency hybrid speech pitch engine with phase locking, pitch-synchronous voiced processing, voiced/unvoiced protection and transient handling.
- Added Deep / Heavy, High / Nasal Rap, Dark Vocal, Radio, Robot and Tiny / High Voice presets plus Custom mode.
- Added character EQ, drive saturation, aligned dry/wet mixing, final output gain, radio band-pass, robot ring modulation and a restrained Tiny doubler.
- Added lock-free block-boundary settings delivery on the existing microphone worker with no callback allocation, file access, device query or mutex wait.
- Added up to 32 validated user presets with atomic config persistence, safe fallback for malformed data and inline save/update/delete controls.
- Added configurable previous-preset, next-preset and Voice Effects bypass global hotkeys.
- Added runtime telemetry for average/maximum processing time, deadline misses, queue peak, dropped input frames and rejected settings updates.
- Added dedicated settings, preset-cycle, processor, runtime, robustness, NaN/Inf, clipping, reset, transition and 10 ms deadline tests.
- Preserved the existing stereo crosstalk cancellation, WebRTC AEC3, RNNoise, dynamics and output-routing behavior around the new processing stage.
- Kept the portable release standalone with no new runtime service, installer, dynamic DSP dependency or user-installed framework.
- Removed a captured AEC diagnostic ZIP from version control and ignored future diagnostic capture archives.

## 2.1.2 - 2026-07-28

- Preserved the selected monitor output as an immediately active two-channel WebRTC AEC3 render reference so channel-specific USB-headset crosstalk is not lost during mono downmix or delayed stereo detection.
- Added an automatic stereo crosstalk canceller ahead of AEC3: GCC-PHAT delay discovery, validated dual-channel NLMS leakage modeling, and a double-talk-aware residual blocker adapt to each output/microphone device without hard-coded latency values.
- Added an aggressive, validated AEC3 suppressor profile for strong playback bleed while retaining WebRTC's default near-end tuning for double-talk protection.
- Fixed endpoint and process-excluded loopback underruns being reported as valid silent reference blocks.
- Switched post-render loopback to AEC3's internal delay tracking instead of forcing a fixed 10 ms device-independent hint.
- Extended AEC processor, runtime and render-reference tests for stereo references and safe underrun recovery.

## 2.1.1 - 2026-07-27

- Fixed AEC3 echo cancellation so voice-chat, game and other system audio played through the selected monitor device is captured through WASAPI loopback and supplied as the far-end reference.
- Kept microphone monitoring out of the endpoint reference to prevent the user's own processed voice from being treated as echo.
- Preserved the previous soundboard-only render reference as a safe fallback when WASAPI loopback is unavailable.
- Reduced the AEC stream-delay hint for post-render loopback references and bounded the loopback queue to avoid stale reference audio.

## 2.1.0 - 2026-07-27

- Added a strict standalone runtime path layer with explicit portable mode, LocalAppData fallback, writability checks and first-run default copying.
- Embedded an `asInvoker` manifest, added elevated-process warnings for drag-and-drop reliability, and packaged `portable.flag` plus a private `tools` directory.
- Added application-path tests and documented the no-admin, no-runtime-install standalone contract.
- Added per-binding fade-in and fade-out envelopes with millisecond controls.
- Applied fade-out consistently to selected-session stop, toggle/loop stop, and Stop All while keeping shutdown/reload teardown immediate.

- Added optional WebRTC AEC3 acoustic echo cancellation with a soundboard monitor-reference path.
- Added high-pass filtering, RNNoise suppression, automatic gain control, compression and limiting.
- Added raw/processed microphone meters, AEC telemetry, presets and temporary processed-microphone monitoring.
- Added a pinned vcpkg manifest feature and production AEC build path.
- Added generated license notices for WebRTC and its transitive static dependencies.
- Kept a dependency-free build with AEC unavailable when WebRTC is disabled.
- Added the playback-session foundation with stable runtime IDs, live position/duration snapshots and per-session pause, resume, stop, seek and volume APIs.
- Added an Active Playbacks tab with live session rows, selection-preserving refresh, pause/resume, per-session stop, seek and volume controls.
- Fixed sub-second playback timing display and made the seek thumb stable during mouse and keyboard interaction.
- Prevented committed seeks from snapping back to stale runtime positions while the audio cursor catches up.
- Added an `ignore` retrigger policy with deterministic behavior for playing and paused sessions.
- Added dedicated playback-policy tests and config round-trip coverage for every serialized mode.
- Made config replacement atomic on Windows and kept `config.txt.bak` as the last known-good saved configuration.
- Added multi-file and recursive folder import through Windows drag and drop and the file picker.
- Added a shared collision-safe importer that copies external audio into `sounds/Imported`, keeps in-tree files in place and rejects unsupported formats without overwriting existing files.
- Added cancellable background URL import and local-media conversion to WAV through bundled `yt-dlp`, Deno, FFmpeg and ffprobe binaries verified against pinned SHA-256 hashes.
- Added an audio editor embedded in the main window with asynchronous WAV load/save, monitor-output preview, buffered waveform rendering, transport, seek, zoom, selection and precision trim.
- Added cut, copy, paste, undo, redo, gain, normalize, fade-in, fade-out, mono conversion, selection silencing and boundary-silence trimming.
- Added `I` and `O` shortcuts to set trim start and end at the current playhead while preserving the opposite selection boundary when possible.
- Polished the editor owner-draw controls to use the same dark surfaces, rounded geometry and redraw behavior as the main control panel.
- Made the editor toolbar responsive, aligned glyph and text content, themed native edit/scroll controls, matched the light palette and moved recoverable validation errors into the inline status area.
- Added a complete fresh-test aggregate target and hardened portable-release verification for package allowlists, tool hashes, x64 GUI PE metadata, hidden artifacts and generated SHA-256 files.

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
