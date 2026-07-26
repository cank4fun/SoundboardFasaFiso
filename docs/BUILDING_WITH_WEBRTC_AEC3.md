# Building with WebRTC AEC3

SoundBoardFasaFiso can be built with acoustic echo cancellation backed by the
WebRTC Audio Processing Module. The feature is optional at configure time so a
plain build keeps the small, dependency-free v2 runtime.

## Reproducible dependency manifest

The repository contains `vcpkg.json` with:

- a pinned vcpkg registry baseline;
- the `webrtc` port fixed to `2026-03-17#1`;
- an opt-in manifest feature named `webrtc-aec3`.

Use the `x64-windows-static` triplet. The application and WebRTC are then linked
with the static MSVC runtime used by the project. Do not use a different vcpkg
checkout for an official release without intentionally updating and retesting
the manifest baseline.

## One-time vcpkg setup

From **x64 Native Tools Command Prompt for VS 2022**:

```bat
set "VCPKG_ROOT=C:\path\to\vcpkg"
git clone https://github.com/microsoft/vcpkg.git "%VCPKG_ROOT%"
cd /d "%VCPKG_ROOT%"
git checkout cd61e1e26a038e82d6550a3ebbe0fbbfe7da78e3
bootstrap-vcpkg.bat -disableMetrics
```

An existing standalone vcpkg checkout can be reused after checking out the same
commit and bootstrapping it again.

## Configure and build

```bat
set "SOUNDBOARD_ROOT=C:\path\to\SoundBoardFasaFiso"
set "VCPKG_ROOT=C:\path\to\vcpkg"
cd /d "%SOUNDBOARD_ROOT%"

cmake -S . -B out\build\x64-AEC3-Release -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  "-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
  -DVCPKG_MANIFEST_FEATURES=webrtc-aec3 ^
  -DSOUNDBOARD_ENABLE_WEBRTC_AEC3=ON ^
  -DSOUNDBOARD_BUILD_WEBRTC_AEC3_TESTS=ON ^
  -DSOUNDBOARD_WARNINGS_AS_ERRORS=ON

cmake --build out\build\x64-AEC3-Release ^
  --target SoundBoardFasaFiso SoundBoardFasaFisoTests --parallel
ctest --test-dir out\build\x64-AEC3-Release --output-on-failure
cmake --build out\build\x64-AEC3-Release --target PortableRelease --parallel
```

`ctest` does not compile test executables. Build `SoundBoardFasaFisoTests` first
so the suite cannot run stale binaries from an earlier source revision.

The first manifest install is large and can take a while. Later builds can reuse
the vcpkg binary cache.

## Build switches

- `SOUNDBOARD_ENABLE_WEBRTC_AEC3=ON` compiles AEC into the application.
- `SOUNDBOARD_BUILD_WEBRTC_AEC3_TESTS=ON` adds the WebRTC smoke, processor,
  render-reference and live microphone runtime tests when `BUILD_TESTING=ON`.
- `SOUNDBOARD_WEBRTC_TARGET` can override imported-target detection if the
  package changes its exported CMake target name.
- `SOUNDBOARD_BUILD_WEBRTC_AEC3_SPIKE` remains only as a deprecated temporary
  alias for old local build directories.

## Packaging and licenses

AEC release builds generate `WEBRTC_THIRD_PARTY_NOTICES.txt` from every
`share/<port>/copyright` file in the build-local manifest installation. The
portable ZIP includes this generated file beside the project's normal
`THIRD_PARTY_NOTICES.txt`.

Notice generation is intentionally fatal when the vcpkg share directory or its
copyright files are missing. An AEC release must not be published without the
notices for WebRTC and its transitive static dependencies.

## Dependency-free build

The ordinary build remains available without vcpkg or WebRTC:

```bat
cmake -S . -B out\build\x64-Release -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DSOUNDBOARD_ENABLE_WEBRTC_AEC3=OFF

cmake --build out\build\x64-Release --parallel
```

That build keeps the AEC control unavailable while preserving the stored config
value for a later AEC-enabled build.
