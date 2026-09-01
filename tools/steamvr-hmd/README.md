# SteamVR NULL / PIMAX buttons

Install with PowerShell 7.5 or newer (keeps JSON date strings unchanged):

```powershell
pwsh -NoProfile -File ./tools/steamvr-hmd/Install-SteamVR-HMD-Launchers.ps1
```

The installer copies the script into `%LOCALAPPDATA%\CSX\SteamVR-HMD` and
creates three shortcuts on the current Windows user's desktop (including a
OneDrive desktop). No repository or Codex plugin is needed to run the installed
copy. Installation does not change SteamVR settings or start SteamVR. Use
`-Force` when deliberately updating an existing installation.

-   **SteamVR NULL**: close SteamVR, click the button; select the null display and
    request SteamVR startup.
-   **SteamVR PIMAX**: close SteamVR, connect the headset and start Pimax Play,
    then click the button; select normal headset discovery and request startup.
-   **SteamVR Toggle**: close SteamVR, click to switch settings only, then start
    SteamVR yourself. The displayed mode confirms the selection.

The buttons keep their console open so errors and the backup path remain
visible. Press Enter to close it. Pimax mode clears `forcedDriver`, sets
`requireHmd=true`, and disables `driver_null.enable`. NULL mode sets
`forcedDriver="null"`, `requireHmd=false`, and enables `driver_null.enable`.
Both set `activateMultipleDrivers=true`. PIMAX is the normal HMD route; it does
not install Pimax drivers or launch Pimax Play.

Other settings, including `LastKnown`, retain their values. Each actual change
atomically saves an exact, uniquely named `steamvr.vrsettings.*.backup` beside
the settings file and writes UTF-8 without a BOM. Repeated selection of an
already configured mode does not rewrite the settings. Any running SteamVR
process blocks switching; the script does not shut down applications.

The script shares the CSX automation controller's settings lock and refuses
active or interrupted automation transactions. Restore/recover those with
`steamvr-null-control` before using these manual buttons. These buttons select
the display mode only; use the automation toolkit for head-pose qualification,
external display-driver isolation, and measurement runs.

```powershell
# Show configured mode without changing settings.
./Toggle-SteamVR-HMD.ps1 -Mode Status

# Preview, switch without launching, or switch and launch.
./Toggle-SteamVR-HMD.ps1 -Mode Pimax -WhatIf
./Toggle-SteamVR-HMD.ps1 -Mode Null
./Toggle-SteamVR-HMD.ps1 -Mode Pimax -StartSteamVR
```

Steam settings are discovered through the current user's Steam registry key.
Startup uses the first active runtime recorded in OpenVR, which supports
SteamVR installed in another library. Explicit `-SettingsPath`, `-SteamVRRoot`,
and `-OpenVRPathsPath` parameters support other installations. A missing selected
file/runtime is an error. Startup requested means the launcher was invoked;
it does not certify a connected or tracking headset.

To revert a manual change, close SteamVR and copy the desired exact `.backup`
over `steamvr.vrsettings`. Retain backups until you no longer need them.

Fixture tests (no live SteamVR settings or startup):

```powershell
pwsh -NoProfile -File ./tests/steamvr_hmd_launcher_test.ps1
```
