# main-vr-nr: True PBR and 3.20 follow-up

Merge main-VR `64f65c5eaa523b91f97732523b7eeabf411283d2` into NR
`2d476d1b9dca3e8feef43e57b2e9d90f8997fc56`, preserving both histories.
Exactly two source commits were missing:

-   `707c2b2cd`: persist and restore True PBR Vertex AO strength.
-   `64f65c5ea`: label the VR version line as 3.20.0 and update release,
    cache-label, and preset compatibility tooling.

## NR integration

Keep NR's feature registration and legacy settings migration in the shared
feature loader. The True PBR save/load implementation and new default-load
hook match main-VR. Its regression fixture includes the existing NR
serialization header and extracts True PBR's real short name so the entire
merged load-dispatch block remains compiled by the test.

Retain settings-contract revision 8, the NR base and all three NR preset
configurations. Regenerate metadata and fingerprints for CSX 3.20.0-VR;
the base and tier settings differ only in `Version` and
`Preset Compatibility`. NR rendering code, shaders, helpers, and graphics
settings remain unchanged.

Adapt the bounded legacy-preset exception from main-VR's revision 5 to
NR's revision 8. Only the three bundled preset IDs with the old 3.19 to
3.20 range are admitted on 3.20 VR. Tests cover all three IDs and rejection
of revisions 5, 7, and 9. Other runtime, version-range, metadata, and
contract checks remain active. The internal NR build preset is preserved;
SE-only version defaults remain on their existing line.

## Validation

Evidence is retained in the primary repository under
`build/analysis/main-vr-nr-followup-20260927/`.

From the NR worktree:

```powershell
pwsh ./tools/cmake.ps1 --preset ALL -D BUILD_CONTROLLER_TESTS=ON
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target preset_compatibility_test -- /m:1
& 'C:/Program Files/CMake/bin/ctest.exe' --test-dir build/ALL -C Release -R '^(TruePBRSettings|PresetCompatibility|FeaturePresetCompatibilityContract|[Nn]eural.*[Ss]ettings.*)$' --output-on-failure --no-tests=error --timeout 120 --output-junit ../../../../analysis/main-vr-nr-followup-20260927/focused-tests.xml
pwsh ./tests/unified_preset_generator_test.ps1
pwsh ./tools/generate-unified-presets.ps1 -Check
```

The focused build and all five selected CTests pass. True PBR compiles and
executes the merged production dispatch and serializers; the NR settings
tests reuse their unchanged existing binaries. The generator regression
and deterministic output check pass. The build retains its existing
assertion-enabling `/DNDEBUG` to `/UNDEBUG` compiler warning.

`python -m unittest discover -s tests -p <file>` also passes for
`csx_release_test.py` (6 tests), `shader_cache_packaging_test.py` (33),
`fomod_package_test.py` (32), and `csx_distribution_test.py` (8).
The preservation audit verifies the unchanged NR code and preset graphics
values and both imported commit identities.

Scoped whitespace, line-ending, YAML, clang-format, and Prettier hooks
pass. Prettier normalized this record on its first pass. Gersemi is
skipped to preserve unrelated existing CMake formatting.

No DLL, AIO, or in-game validation is run for this follow-up. The existing
production archive still identifies `2d476d1b9` and does not contain these
two updates. The primary checkout remains on `main-VR`.
