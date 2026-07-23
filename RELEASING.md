# Creating a release

The public version is defined by `SOUNDBOARD_VERSION` in `CMakeLists.txt`. The Git tag must match it exactly, including prerelease suffixes. Existing release tags are immutable and must never be moved or reused.

## Release branches

Prepare a release on a dedicated branch created from the intended stable base. For `v2.0.0`, the release branch must be based on `v2.0.0-rc.2` / `main` and must not contain v2.1 microphone-processing or WebRTC AEC3 work.

Before opening the pull request:

```powershell
git status --short --branch
git diff v2.0.0-rc.2..HEAD --stat
git log --oneline v2.0.0-rc.2..HEAD
```

The stable release diff should contain only intentional version, documentation and release-workflow changes unless a separately reviewed blocker fix is required.

## Pre-release checks

From **x64 Native Tools Command Prompt for VS 2022**:

```cmd
rmdir /S /Q out\build\x64-Release 2>nul

cmake -S . -B out/build/x64-Release -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DSOUNDBOARD_WARNINGS_AS_ERRORS=ON

cmake --build out/build/x64-Release --target PortableRelease --parallel
```

Confirm that:

- the Release build completes with warnings treated as errors
- the executable version and portable filename contain the intended version
- the portable ZIP opens from a newly extracted folder on Windows 10/11
- no developer config, logs, build files or personal paths are present
- audio playback, microphone routing, tray behavior, hotkeys, config rollback and device recovery work
- the update check opens only the official GitHub Release page
- the unsigned-build and Smart App Control limitation is present in both readmes
- the portable package includes `LICENSE` and `THIRD_PARTY_NOTICES.txt`
- the generated checksum matches the exact ZIP attached to the release

The package name is versioned:

```text
SoundBoardFasaFiso-v<version>-windows-x64-portable.zip
```

## Merge the stable release

Open a pull request from `release/v2.0.0` to `main`. Wait for the Windows workflow to pass, review the final diff, and merge the pull request without changing the prepared version.

After the merge:

```powershell
git switch main
git pull --ff-only
git status --short --branch
git log -3 --oneline --decorate
```

Build or inspect the workflow artifact from the exact merged commit before tagging.

## Publish the stable release

Create an annotated tag from the verified `main` commit:

```powershell
git tag -a v2.0.0 -m "SoundBoardFasaFiso v2.0.0"
git show --stat v2.0.0
git push origin v2.0.0
```

The **Windows Build and Release** workflow verifies that the tag matches the project version, builds with MSVC and warnings as errors, validates the portable package, produces a SHA-256 checksum, uploads both files as workflow artifacts, and attaches them to the tagged GitHub Release. If `docs/releases/v2.0.0.md` exists, it is used as the release description.

The `v2.0.0` assets are intentionally unsigned. The GitHub Release description and both readmes must state this clearly.

After publication, verify:

- the release is not marked as a prerelease
- the ZIP is named `SoundBoardFasaFiso-v2.0.0-windows-x64-portable.zip`
- the matching `.sha256` file is attached
- the published checksum matches a freshly downloaded ZIP
- the release description contains the unsigned-build warning
- the update checker recognizes `v2.0.0` as the latest stable release

If a tagged workflow fails because of a transient service error, rerun the same workflow. Do not move the tag. If the tagged source itself requires a correction, publish a new patch version instead of rewriting `v2.0.0`.
