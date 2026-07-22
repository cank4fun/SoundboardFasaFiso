# SoundBoardFasaFiso v2.1 Microphone Processing Design

## Status

This document defines the implementation boundary for the v2.1 microphone-processing
work. It is a design checkpoint, not a claim that the DSP features are already present.

The first v2.1 code change, removing the selected Hotkeys binding with the Delete key,
is intentionally independent from this audio work.

## Goals

v2.1 should improve low-volume and noisy microphones locally, before the microphone is
mixed with soundboard audio and sent to the selected outputs.

The minimum user-facing result is:

- processing can be completely disabled;
- old configurations keep the v2.0 microphone behavior;
- raw and processed microphone levels can be inspected separately;
- high-pass filtering, noise suppression, gain control/compression and limiting are
  available;
- safe presets provide useful starting points;
- a filter or dependency failure does not close the control panel or disable soundboard
  playback;
- processing remains local and offline.

Acoustic echo cancellation is not part of the first implementation. It requires a
render/reference signal and delay alignment, so it will remain an experimental later
phase.

## Current v2.0 audio path

The current runtime uses a miniaudio capture device configured for interleaved 32-bit
floating-point stereo samples. The microphone callback currently:

1. calculates a raw peak value;
2. copies the captured frames directly into the main-output microphone ring buffer when
   that route is enabled;
3. copies the same frames into the monitor microphone ring buffer when that route is
   enabled.

Each microphone route is exposed to a miniaudio engine through `ma_pcm_rb` and
`ma_sound`. The capture device can fall back from the requested buffer size to the
Windows default and from the requested sample rate to the device's native sample rate.
Device recovery rebuilds the runtime and reloads the sounds.

The initial v2.1 integration must preserve that proven routing and recovery model.

## Processing format

The internal DSP format will be:

- 48,000 Hz;
- mono;
- 32-bit floating point;
- normalized sample range `[-1.0, 1.0]`;
- 10 ms processing blocks, equal to 480 samples at 48 kHz.

Capture and playback devices are not required to use this exact format. Channel
conversion and resampling belong at the boundary of the processing pipeline.

The processed mono signal will be duplicated to stereo before it is written to the
existing microphone routes. This keeps the current output-engine integration stable
while the DSP core remains voice-oriented and mono.

## Thread model

### Capture callback

The miniaudio capture callback is a real-time path. It must remain bounded and must not:

- allocate or free memory;
- read or write files;
- log normal per-frame activity;
- wait on a mutex, condition variable or another thread;
- initialize, destroy or reconfigure audio objects;
- call GUI code.

When processing is enabled, the callback performs only bounded operations:

1. copy captured frames into a preallocated single-producer/single-consumer input ring;
2. update atomic overflow and device-state counters;
3. return.

When processing is disabled, the current direct-routing path remains available so old
configurations keep their behavior and latency.

### Processing worker

A dedicated worker thread consumes captured frames and owns all mutable DSP state. It:

1. converts the capture format to 48 kHz mono float;
2. accumulates exactly 480 samples;
3. records the raw meter point;
4. runs the enabled processing stages in order;
5. records the processed meter point and clipping state;
6. duplicates mono to stereo;
7. writes frames to the existing main and monitor microphone routes.

All working buffers, converters and filter state are allocated during initialization.
Runtime setting changes are delivered as immutable snapshots or atomics and are applied
at block boundaries.

The worker may sleep or wait when no input is available, but the capture callback must
never wait for the worker.

## Processing order

The intended order is:

1. input channel conversion and resampling;
2. DC blocking / high-pass filter;
3. noise suppression;
4. expander or gate, when added;
5. automatic gain control or compressor;
6. makeup gain, when required;
7. brick-wall or look-ahead-free safety limiter;
8. processed meters and clipping detection;
9. output channel conversion and routing.

The first native-only milestone implements the high-pass filter, compressor, limiter,
pre/post meters and bypass behavior before an external noise-suppression dependency is
introduced.

## Processor interface

The DSP implementation should be separated from `Audio` behind a small interface. The
planned ownership model is one processor instance per active microphone runtime.

A future implementation may use names similar to:

```cpp
struct MicrophoneProcessingSettings;
struct MicrophoneProcessingSnapshot;
class MicrophoneProcessor;
```

The processor should expose operations equivalent to:

```cpp
bool Initialize(
    unsigned int inputSampleRate,
    unsigned int inputChannels,
    const MicrophoneProcessingSettings& settings
);

bool ProcessInput(
    const float* interleavedInput,
    std::size_t inputFrameCount
);

void UpdateSettings(
    const MicrophoneProcessingSettings& settings
);

MicrophoneProcessingSnapshot GetSnapshot() const;
void Reset();
```

The exact API may change during implementation. The important boundary is that the
capture callback does not directly own filter implementations and `Audio` does not need
to know the internal details of each processing engine.

## Meter points

v2.1 needs separate measurements:

- **Raw peak** — after channel conversion and resampling, before filters or gain;
- **Raw RMS** — the same pre-processing point;
- **Processed peak** — after the limiter;
- **Processed RMS** — after the limiter;
- **Clipping indicator** — latched when the pre-limiter signal exceeds the safe range or
  the final signal reaches the configured ceiling;
- **Limiter gain reduction** — useful for diagnostics and later GUI work;
- **Input overflow / output underrun counters** — diagnostics for buffer pressure.

Meter values cross thread boundaries through atomics or a lock-free snapshot. The GUI
must not read mutable filter state directly.

## Failure and bypass behavior

The application must fail open for soundboard playback and fail safely for microphone
processing.

- Missing v2.1 config keys mean processing is disabled.
- If the processing subsystem cannot initialize, log one clear warning and use the
  existing unprocessed microphone route when possible.
- A failed optional filter is bypassed; the remaining chain continues.
- An external noise suppressor error disables that stage rather than terminating the
  audio runtime.
- Input-ring overflow drops the oldest or newest documented block without blocking the
  callback, increments a counter and allows recovery on the next blocks.
- Device disconnect recovery destroys and recreates processor state together with the
  microphone runtime.
- Applying invalid processing settings must trigger the existing transactional config
  rollback rather than leaving a half-configured runtime.

No DSP failure may close the GUI, prevent soundboard-only use or corrupt `config.txt`.

## Configuration and migration

The master switch defaults to off. This guarantees that an existing v2.0 configuration
keeps the current audio path after upgrading.

The initial key family is planned as:

```ini
microphone_processing_enabled=false
microphone_processing_preset=natural

microphone_high_pass_enabled=true
microphone_high_pass_hz=80

microphone_noise_suppression_enabled=true
microphone_noise_suppression_level=balanced

microphone_agc_enabled=true
microphone_agc_target_dbfs=-18.0

microphone_compressor_enabled=true
microphone_compressor_threshold_db=-24.0
microphone_compressor_ratio=3.0
microphone_compressor_attack_ms=10.0
microphone_compressor_release_ms=120.0
microphone_compressor_makeup_db=0.0

microphone_limiter_enabled=true
microphone_limiter_ceiling_db=-1.0
```

These names and ranges are design candidates until the parser and GUI work begins. The
implementation must validate every value and serialize settings deterministically.
Unknown future keys should follow the existing config policy.

## Presets

Presets are complete setting snapshots, not hidden alternative algorithms.

- **Natural** — light high-pass, conservative suppression and gentle dynamics;
- **Clean** — stronger constant-noise reduction with moderate gain control;
- **Strong** — more aggressive suppression and compression for weak microphones;
- **Aggressive** — maximum cleanup for difficult voice-chat conditions, with a clear
  warning that artifacts are more likely.

Changing an individual control after selecting a preset marks the state as custom.
Exact numeric values will be chosen through recorded-sample and live voice-chat tests,
not guessed in the first code commit.

## Noise-suppression dependency gate

No external DSP dependency is added until a Windows/MSVC proof-of-concept answers:

- license and notice requirements;
- reproducible GitHub Actions build;
- static and portable packaging behavior;
- binary-size impact;
- CPU cost on sustained 48 kHz processing;
- algorithmic and buffering latency;
- deterministic artifact origin for signing;
- failure and bypass behavior.

The two candidates remain:

### WebRTC Audio Processing Module

Advantages:

- designed for real-time communication microphone enhancement;
- includes noise suppression and gain-control components;
- provides the later path to echo cancellation;
- supports standalone use.

Risks:

- large and fast-moving dependency graph;
- GN-oriented upstream build and more difficult CMake/MSVC integration;
- larger source, build and signing surface than a focused denoiser.

### RNNoise

Advantages:

- focused C implementation;
- BSD-3-Clause license;
- voice-noise suppression at 48 kHz;
- smaller conceptual integration surface.

Risks:

- noise suppression only; AGC, compression and AEC remain our responsibility;
- model data and CPU/SIMD behavior must be packaged and tested carefully;
- model/frame requirements must fit the processing worker without callback blocking.

The dependency decision is made after both build spikes are measured. It is not made by
feature count alone.

## AEC boundary

Echo cancellation is deferred until the non-AEC chain is stable. AEC needs a reverse or
render stream representing what can reach the user's speakers. Candidate references are:

- the application's main/monitor render mix before device output;
- WASAPI loopback capture of the selected physical playback device.

Both require delay estimation and alignment with captured microphone blocks. Headphone
users receive less benefit, so AEC remains disabled by default and experimental when it
is eventually introduced.

## Latency and performance targets

Initial engineering targets:

- no allocations or blocking operations in the capture callback;
- one 10 ms DSP block of intentional buffering;
- no unbounded input or output queues;
- average processing time comfortably below the 10 ms block deadline;
- added non-AEC processing latency target at or below 20 ms;
- overload is visible through counters and logs;
- overload degrades by bypassing optional work, not by crashing the application.

Measurements must include Debug and warnings-as-errors Release builds, microphone-only
routing, main plus monitor routing and device reconnect scenarios.

## Implementation phases

### Phase 1 — Native processing foundation

- settings and config migration;
- processor interface and worker lifecycle;
- preallocated rings and 48 kHz mono block adapter;
- high-pass filter;
- compressor;
- limiter;
- raw/processed meters;
- bypass and failure tests.

### Phase 2 — External noise suppression and gain behavior

- build spikes for WebRTC APM and RNNoise;
- dependency decision record;
- selected suppressor integration;
- AGC behavior and interaction with compressor/limiter;
- CPU, latency, packaging and license tests.

### Phase 3 — GUI and presets

- master enable;
- preset selector;
- individual filter controls;
- raw and processed meters;
- clipping indication;
- processed microphone monitor/test control;
- reset and warning states.

### Phase 4 — Experimental AEC

- render-reference capture;
- reverse-stream buffering;
- delay alignment;
- headphone and speaker test matrix;
- disabled-by-default experimental setting.

## Test matrix

Every microphone-processing milestone must cover:

- processing disabled reproduces v2.0 behavior;
- old config without v2.1 keys;
- 44.1 kHz, 48 kHz and native-rate fallback devices;
- mono and stereo capture presentation;
- main-only, monitor-only and main-plus-monitor routes;
- processing stage enable/disable while applying settings;
- silence, quiet speech, loud speech, constant hiss, keyboard transients and clipping;
- device disconnect and automatic recovery;
- input overflow and simulated slow processing;
- repeated save/apply rollback;
- clean Release build with warnings as errors;
- fresh portable ZIP and third-party notices.

## Primary-source checkpoints

Before implementing an external dependency, re-check the current upstream sources:

- WebRTC APM overview and API:
  `https://webrtc.googlesource.com/src/+/refs/heads/main/modules/audio_processing/`
- WebRTC license and patent grant:
  `https://webrtc.googlesource.com/src/+/refs/heads/main/LICENSE`
  and `https://webrtc.googlesource.com/src/+/refs/heads/main/PATENTS`
- RNNoise upstream:
  `https://gitlab.xiph.org/xiph/rnnoise`
- miniaudio manual:
  `https://miniaud.io/docs/manual/`

The exact upstream revision must be pinned when a dependency is vendored.
