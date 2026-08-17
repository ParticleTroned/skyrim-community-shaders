# Calibration artifact storage policy

Status: active

## Safety rule

MO2 `overwrite\Root` is temporary staging, not an evidence archive. Persistent calibration artifacts must remain outside both the MO2 instance and the SkyrimVR game directory. RootBuilder treats every directory beneath `overwrite\Root` as deployable game-root content; large evidence trees there can therefore trigger hashing, virtual-tree construction, and full duplication into the game directory.

The repository-local setting `csx.calibrationArchiveRoot` is the canonical workstation archive root. Calibration tools resolve their default output to:

`<archive root>\Sessions\<yyyy-MM-dd>\CSX Baselines\<collection>`

An explicit `-OutputRoot` remains available for controlled exceptions, but tools must reject a configured archive root beneath MO2 or SkyrimVR.

## Session boundary

Before starting MO2:

1. Confirm `overwrite\Root\CSX Baselines` and `overwrite\Root\Screenshots` contain no retained prior-session evidence.
2. Confirm the configured archive drive is available.
3. Use the calibration scripts' archive-resolved defaults rather than an overwrite path.

After Skyrim and MO2 have stopped, run:

```powershell
.\tools\archive-preset-calibration-session.ps1
```

The finaliser refuses to run while `ModOrganizer`, `SkyrimVR`, or `sksevr_loader` is active. It moves any accidental `CSX Baselines` and `Screenshots` staging trees into the dated `MO2-overwrite` archive with bounded eight-worker `robocopy`, preserves timestamps, and records a log. A RootBuilder or MO2 launch must not follow until this finalisation check has completed.

## Evidence references

Committed measurement records should prefer stable campaign locators and hashes. Machine-local absolute paths are operational hints and must be updated when an archive is relocated; they are not evidence identity.
