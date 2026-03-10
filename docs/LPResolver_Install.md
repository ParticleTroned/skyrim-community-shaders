# LP Conflict Resolver - User Install Guide

## 1. What Users Download

Publish two artifacts:

- `LPConflictResolver-Setup.exe` (recommended)
- `LPConflictResolver-Portable.zip` (advanced users)

No Python install is required for end users.

## 2. Install

### Setup EXE

1. Run `LPConflictResolver-Setup.exe`.
2. Finish installer.
3. Start from desktop/start menu shortcut.

### Portable ZIP

1. Extract zip to a permanent folder.
2. Run `LPConflictResolver.exe`.

## 3. Configure For MO2

In the app:

1. Set `MO2 Root` (folder containing `mods` and `profiles`).
2. Set `Profile Path` (for example `C:\Modlists\profiles\Borealis`).
3. Set `PL Source` to `NIF (ENB Particle Lights)` unless you explicitly need JSON mode.
4. Click `Scan`.

## 4. Use Decisions And Export Patch

1. Select conflict rows.
2. Choose decision:
   - Ignore
   - Keep Highest Priority LP
   - Choose Specific LP Entry
   - Disable LP
3. Save decisions (`resolver_decisions.json`) for re-use.
4. Click `Export Patch`.

Patch is written to:

- `MO2\mods\LP_ConflictPatch\...`

Enable this patch mod in MO2 at high priority.

## 5. MO2 Executable Entry (Optional)

Users can add `LPConflictResolver.exe` to MO2 executables:

1. Open MO2.
2. Add executable.
3. Point to `LPConflictResolver.exe`.
4. Launch from MO2 toolbar.

This helps users keep consistent profile context.

