# Water profile persistence

Water Parallax Strength and Parallax Quality were written correctly by
the settings serializer but reset when base or location profiles were
loaded or imported. The legacy water migration rebuilt each native water
profile from a list of eight former Unified Water fields. This discarded
the two parallax fields and Caustics Strength, Tiling, Speed and Dispersion.
The global adjustment layer did not pass through that migration.

Migration now merges the fields emitted by the current water-profile
serializer, retaining explicit values with the expected JSON type.
Missing or malformed fields inherit the migrated legacy global value or
the current default. Existing forced-global precedence, explicit-profile
markers, unknown-field removal and numeric bounds remain in effect.
Parallax Quality is bounded before JSON narrows a wide numeric value to
an integer, preventing overflow from bypassing the intended `4` to `64`
range. The owned merged JSON object is moved into the validation helper.
This applies to SE, AE and VR and adds no work to rendering or shaders.

The `WaterAppearanceSettings` controller test compiles the production
serializer, migration function and sanitizer with MSVC. It exercises
repeated JSON save/reload, the migration shared by preset imports, legacy
inheritance and explicit precedence, malformed fields, bounds and older
profiles. On source `dd6ca74d30a3e909d95df8c40e71846648c908ff`, four of
its five cases fail, reproducing the loss; the legacy-default case passes.
The adversarial review added a sixth case for wide integers, extreme
floating-point values and fractional quality. It failed before the
pre-conversion bound was added.

After both corrections, all six tests pass with
`pwsh ./tools/run-msvc-command.ps1 python tests/water_appearance_settings_test.py -v`,
using the installed nlohmann JSON include directory through
`CSX_JSON_INCLUDE`. The suite is registered as `WaterAppearanceSettings`
under `ControllerTests`. No in-game save/restart test has been performed
for this change.

Already overwritten values cannot be recovered automatically. Values
still present in a saved settings or preset file are preserved by the fix.
