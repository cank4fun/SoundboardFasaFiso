# Code signing and release integrity policy

## Current signing status

Official SoundBoardFasaFiso GitHub release artifacts are currently **unsigned**.
The project does not currently use a SignPath Foundation certificate or another
public code-signing certificate, and no release may claim a publisher identity
or certificate that was not actually applied to the distributed executable.

The project applied to the SignPath Foundation program at an early stage but
was not approved because it did not yet have sufficient public adoption and
independent visibility. The project may reapply after broader community use. A
paid certificate is not planned at this stage, and missing code signing does
not block development or release publication.

Unsigned Windows builds can trigger Microsoft Defender SmartScreen warnings,
and Windows 11 Smart App Control or another application-control policy can
block them. Users should not weaken system security solely to run the
application. A SHA-256 checksum verifies file identity but does not provide
publisher authentication.

## Project roles

- Committer and reviewer: [cank4fun](https://github.com/cank4fun)
- Release approver: [cank4fun](https://github.com/cank4fun)

Changes submitted by contributors who do not have direct repository write
access must be reviewed before they are merged.

## Official release provenance

Official release binaries are built from this public source repository by
GitHub Actions on GitHub-hosted Windows runners. A release is considered
official only when all of the following are true:

- the source commit is reachable from the public repository;
- the immutable Git tag exactly matches `SOUNDBOARD_VERSION` in
  `CMakeLists.txt`;
- the portable ZIP and its `.sha256` file were produced by the official Windows
  workflow; and
- the assets are attached to the GitHub Release for that tag.

Users should download artifacts only from the repository's GitHub Releases
page and compare the ZIP with the attached checksum:

```powershell
Get-FileHash .\SoundBoardFasaFiso-v*-windows-x64-portable.zip -Algorithm SHA256
```

A matching hash confirms that the downloaded archive is byte-for-byte
identical to the published release asset. It does not replace Authenticode
signing.

AEC-enabled releases use the pinned `vcpkg.json` manifest and include generated
license notices for WebRTC and all transitive static dependencies present in
the isolated manifest installation.

## Privacy policy

SoundBoardFasaFiso does not transfer user audio, microphone content,
configuration, logs, sound files or other personal information to external
systems.

The application contacts GitHub only when the user enables the optional startup
update check or manually requests an update check. The updater reads public
release information and does not upload user data.

The application does not automatically download, replace or execute updates.

## Future signed releases

Code signing may be added later when an appropriate certificate or signing
service is available. Any future signed release must:

- be built from a public commit and immutable release tag;
- be produced by, or traceable to, the official repository workflow;
- require explicit approval by the release approver;
- document the certificate subject and verification procedure;
- keep certificates and private keys outside this repository and maintainer
  source trees; and
- retain the public checksum and build-provenance record.

Until those conditions are met, releases remain explicitly unsigned.
