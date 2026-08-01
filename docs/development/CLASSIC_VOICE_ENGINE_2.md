# Classic Voice Engine 2

Classic Voice Engine 2 is the deterministic, local DSP path for the v2 Voice
Effects chain. It does not use inference, model files, network access or a
background service.

The microphone order remains:

```text
stereo crosstalk cancellation
-> WebRTC AEC3
-> high-pass filter
-> RNNoise
-> Voice Effects / Classic Voice Engine 2
-> AGC
-> compressor
-> Voice Effects output gain
-> limiter
-> routing
```

## 6A - Speech Analysis Core

`SpeechAnalysisCore` is a fixed-size, allocation-free analysis stage for the
existing 48 kHz Voice Effects worker. Every 1024-sample analysis frame reuses
the processor's existing 513-bin magnitude spectrum instead of running a
second FFT.

The frame contract provides:

- fundamental frequency and pitch period across the 70-700 Hz speech range;
- raw pitch confidence and smoothed voiced/unvoiced confidence;
- adaptive speech activity, RMS and peak level;
- spectral flatness, centroid and high-band energy ratio;
- onset and transient probability;
- unvoiced probability;
- a reusable smoothed spectral envelope for later formant processing.

Pitch detection uses normalized time-domain correlation, continuity scoring,
first-strong-peak octave correction and sub-sample peak interpolation. Silence,
invalid samples and broadband noise reduce confidence rather than forcing a
pitch result.

The analysis state is reset together with `VoiceEffectsProcessor`. Processing
performs no heap allocation, file access, device query, lock or dependency
call. Existing fixed Voice Effects latency remains unchanged.

## 6B - Pitch Engine 2

`PitchEngine2` owns the fixed-latency dry delay, pitch-synchronous time-domain
shifter and voiced hybrid controller. It combines the existing phase-locked
spectral path with two complementary raised-cosine grains instead of forcing
one algorithm across vowels, consonants and attacks.

For confident voiced frames, the grain span follows an even number of detected
fundamental periods. The two read heads therefore remain separated by whole
glottal cycles, which reduces combing and phase cancellation while pitch is
shifted. Fractional delay reads use bounded cubic interpolation. Grain length,
blend and level matching are smoothed without allocating or locking.

The 6A analysis contract controls the hybrid path:

- pitch and voicing confidence determine how much time-domain pitch is used;
- speech activity prevents silence and noise from driving voiced grains;
- onset and transient detection withdraw the voiced blend and safely re-anchor
  the grain phase;
- unvoiced probability preserves consonants and high-frequency articulation;
- independent formant moves reduce incompatible time-domain blending until the
  dedicated 6C engine takes ownership of the spectral envelope.

Pitch Engine 2 sanitizes invalid samples and controls, performs no heap
allocation, and keeps the existing 768-sample Voice Effects latency. It adds no
model, runtime service or third-party dependency.

## 6C - Formant Engine 2

`FormantEngine2` consumes the 6A smoothed spectral envelope and produces a
bounded per-bin correction independent from the pitch ratio. Envelope
resampling is performed in the log domain, then spatial and frame-to-frame
smoothing prevent isolated resonances and zipper noise.

The engine backs off around attacks and unvoiced consonants, fades correction
below the useful speech band and above the articulation band, limits individual
bin gain, and applies partial spectral-energy normalization. It does not add a
second FFT, allocation, lock or algorithmic-delay stage.

## 6D - Vocal Weight Engine 2

`VocalWeightEngine2` replaces the original body stage with a pitch-free,
analysis-guided parallel path. Detected F0, voicing, speech activity, RMS, onset
and unvoiced probability control an adaptive chest band and the amount of
parallel density. A second band limits boxiness around the low-mid region.

The contribution is smoothed and bounded per sample. Silence, consonants and
transients withdraw reinforcement rather than being mistaken for chest tone.
Pitch and formant values are not modified.

## 6E - Voice Polish Engine 2

`VoicePolishEngine2` owns four independently bypassable post-processing
modules:

- low-shelf, peaking and high-shelf parametric EQ with adjustable frequencies
  and mid-band Q;
- a split-band de-esser with smoothed high-band attenuation;
- a speech- and onset-aware gate/expander with hold protection; and
- a soft-knee compressor with bounded gain reduction and restrained makeup.

All settings and filter coefficients are smoothed. Invalid inputs are sanitized
and module output is bounded. The engine remains allocation-free and runs in
the existing Voice Effects worker before the microphone AGC.

## 6F - Modular rack and portable presets

The EQ, de-esser, gate/expander and compressor run through a validated
`VoiceEffectRackOrder`. Live order changes briefly cross toward the aligned dry
signal, reset module state and fade the requested order back in to reduce
clicks and stale detector state. Module enable flags remain independent from
the rack order.

User settings and rack order can be exported to versioned `.sbffvoice` files.
The file layer enforces a small size limit, strict UTF-8 and field validation,
a checksum, deterministic safe names and transactional replacement with
rollback. Files are discovered only inside the portable `voice-presets`
directory.

## 6G - Stabilization contracts

The final stabilization pass makes gate onset protection open from a fully
closed state, includes rack order when matching or cycling presets, and writes
floating-point configuration values with round-trip-safe precision. These
contracts prevent clipped syllable starts, incorrect built-in-preset matches
and gradual parameter drift across save/load cycles.

## Stage boundaries

- 6B consumes pitch period, pitch confidence, voicing, onset and transient data.
- 6C consumes the spectral envelope and voiced/unvoiced data.
- 6D consumes speech activity, voicing, RMS and low-band behavior.
- 6E can consume speech activity, centroid, high-band ratio and transient data.
- 6F owns effect ordering and preset serialization; it does not move the Voice
  Effects stage outside the RNNoise-to-AGC slot.

## 6H - Deterministic self-test and diagnostics

`RunVoiceEngineSelfTest` is an offline, dependency-free health check. It is
invoked only from the Voice Effects page or its dedicated automated test; it is
never called by the microphone callback or worker thread.

The self-test generates deterministic voiced, consonant and level-changing
blocks and verifies:

- the 48 kHz / 480-sample processor contract and fixed 768-sample active
  latency;
- sample-transparent disabled processing and settled dynamic bypass;
- finite, bounded and non-silent output through the complete 6A-6F chain;
- observable behavior changes when the modular rack order changes;
- clean processor reset and reinitialization.

The compact result remains inline beside the existing runtime budget report.
No recording is made, no device is opened, no file is written and no model or
network service is used. This self-test catches deterministic DSP and state
regressions; it does not replace the final physical microphone, AEC, routing
and listening tests.
