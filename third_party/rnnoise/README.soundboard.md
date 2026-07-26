# RNNoise vendoring notes

SoundBoardFasaFiso vendors the scalar C implementation of RNNoise v0.2 for the
microphone noise-suppression spike.

- Upstream project: Xiph.Org RNNoise
- Upstream tag: `v0.2`
- Source commit: `904a876dce1f9ab8860c0a5000ed151f9f6eef58`
- Model version: `0b50c45`
- Model archive SHA-256:
  `4AC81C5C0884EC4BD5907026AAAE16209B7B76CD9D7F71AF582094A2F98F4B43`
- Selected spike model: `rnnoise_data_little.c`
- Build mode: scalar C; SIMD/runtime dispatch is intentionally deferred

The little model is provisional until real microphone A/B quality testing is
complete. Run the importer from the repository and provide the verified local
checkout explicitly:

```powershell
.\tools\Import-RnNoiseV02.ps1 -SourceRoot "C:\path\to\rnnoise"
```

The complete upstream BSD-3-Clause license is stored in `COPYING` after import.
