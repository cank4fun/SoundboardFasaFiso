# WebRTC AEC3 build spike (completed)

This historical spike proved that the WebRTC Audio Processing API could be
compiled, linked, created, and fed 10 ms render/capture blocks with the
project's MSVC/CMake toolchain. The gate passed and AEC was later integrated
into the live microphone pipeline.

Production build instructions now live in
[`docs/BUILDING_WITH_WEBRTC_AEC3.md`](../BUILDING_WITH_WEBRTC_AEC3.md).
`SOUNDBOARD_BUILD_WEBRTC_AEC3_SPIKE` remains only as a deprecated compatibility
alias for old local build directories.

## Dependency choice for the spike

The spike uses the `webrtc` vcpkg port and its `unofficial-webrtc` CMake
package. Use the static Windows triplet so its CRT linkage matches this
project's `/MT` setting.

Do not add the dependency to a release build yet. First record:

- whether the package builds successfully with the installed Visual Studio and
  Windows SDK;
- the vcpkg install size and elapsed build time;
- the final smoke executable size;
- the exact imported CMake target selected during configuration;
- the WebRTC and transitive third-party notices that would need shipping.

## Build from x64 Native Tools Command Prompt

Install vcpkg once, outside this repository:

```bat
cd /d C:\Dev
git clone https://github.com/microsoft/vcpkg.git
cd /d C:\Dev\vcpkg
bootstrap-vcpkg.bat -disableMetrics
vcpkg install webrtc:x64-windows-static
```

Configure an isolated build directory:

```bat
cd /d C:\Dev\SoundBoardFasaFiso-GitHub

cmake -S . -B out\build\x64-AEC3-Spike -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=C:\Dev\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
  -DSOUNDBOARD_BUILD_WEBRTC_AEC3_SPIKE=ON
```

The configure output must contain a line similar to:

```text
-- WebRTC AEC3 spike target: unofficial::webrtc::webrtc
```

The exact target name may differ. If automatic selection fails, inspect the
package's config file and configure again with:

```bat
-DSOUNDBOARD_WEBRTC_TARGET=<imported-target-name>
```

Build and run only the smoke target:

```bat
cmake --build out\build\x64-AEC3-Spike ^
  --target SoundBoardFasaFisoWebRtcAec3Smoke --parallel

ctest --test-dir out\build\x64-AEC3-Spike ^
  -R WebRtcAec3BuildSmoke --output-on-failure
```

Expected program output:

```text
WebRTC AEC3 smoke test passed: 200 render/capture blocks at 48 kHz mono.
```

## Pass criteria

The spike passes only when all of the following are true:

1. The vcpkg package builds with MSVC using `x64-windows-static`.
2. CMake discovers and links the imported WebRTC target.
3. The smoke executable creates `AudioProcessing` with echo cancellation
   enabled.
4. Two hundred 10 ms render/capture pairs return `kNoError` and finite samples.
5. The ordinary `x64-RC2` build remains unaffected with the spike option off.

After this gate passes, live integration should be a separate commit. The
render reference must contain only soundboard audio actually sent to the
physical monitor output; microphone monitoring and the CABLE output must not be
fed back as far-end audio.
