# Calibration artifact storage policy

Status: active

## Safety rule

MO2 `overwrite\Root` is temporary staging, not an evidence archive. Persistent calibration artifacts must remain outside both the MO2 instance and the SkyrimVR game directory. RootBuilder treats every directory beneath `overwrite\Root` as deployable game-root content; large evidence trees there can therefore trigger hashing, virtual-tree construction, and full duplication into the game directory.

The repository-local setting `csx.calibrationStagingRoot` is the canonical fast, temporary capture root. Calibration tools resolve their default output to:

`<staging root>\Sessions\<yyyy-MM-dd>\CSX Baselines\<collection>`

The repository-local setting `csx.calibrationArchiveRoot` is the persistent archive destination. On this workstation, staging is on the D: NVMe and the archive is on the L: HDD. Both roots must remain outside MO2 and SkyrimVR. An explicit `-OutputRoot` remains available for controlled exceptions.

## Session boundary

Before starting MO2:

1. Confirm `overwrite\Root\CSX Baselines` and `overwrite\Root\Screenshots` contain no retained prior-session evidence.
2. Confirm the configured fast staging drive is available and has enough free space for the bounded session.
3. Use the calibration scripts' staging-resolved defaults rather than an overwrite or game path.

After Skyrim and MO2 have stopped, run:

```powershell
.\tools\archive-preset-calibration-session.ps1
```

The finaliser refuses to run while `ModOrganizer`, `SkyrimVR`, or `sksevr_loader` is active. It moves the dated fast-staging `CSX Baselines` tree into the corresponding persistent archive session and moves any accidental overwrite `CSX Baselines` or `Screenshots` trees into that session's `MO2-overwrite` quarantine. It uses bounded eight-worker `robocopy`, preserves timestamps, and records a log. A later calibration session or MO2 launch must not begin until this finalisation check has completed.

The fast staging root is session-bounded, not a second archive. Failed or rejected runs may remain there until finalisation so their failure metadata is preserved, but no completed session should remain on D: after its finaliser succeeds.

## Evidence references

Committed measurement records should prefer stable campaign locators and hashes. Machine-local absolute paths are operational hints and must be updated when an archive is relocated; they are not evidence identity.
