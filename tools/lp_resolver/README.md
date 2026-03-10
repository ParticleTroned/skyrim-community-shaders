# LP Conflict Resolver

Desktop and CLI tool for Mod Organizer 2 (MO2) that scans:

- Light Placer JSON entries (for example CS Light / True Light packs)
- Particle Light targets from ENB Particle Lights NIF files (default)

It detects:

- `duplicate_exact`
- `duplicate_divergent`
- `lp_vs_pl_overlap`

and lets users save decisions and export a generated patch mod.

## 1. CLI Usage

Run from repository root:

```powershell
python -m tools.lp_resolver `
  --mo2-root "C:\Modlists" `
  --profile-path "C:\Modlists\profiles\Borealis" `
  --output-dir "dist\lp_resolver" `
  --pl-source nif `
  --verbose
```

Useful filters:

- `--only-overlap`
- `--ignore-duplicate-exact`
- `--cross-mod-lp-duplicates-only`

Examples:

```powershell
# Only LP vs PL conflicts
python -m tools.lp_resolver --mo2-root "C:\Modlists" --profile-path "C:\Modlists\profiles\Borealis" --pl-source nif --only-overlap
```

```powershell
# Focus LP duplicates that span different mods
python -m tools.lp_resolver --mo2-root "C:\Modlists" --profile-path "C:\Modlists\profiles\Borealis" --cross-mod-lp-duplicates-only
```

## 2. GUI Usage

Run GUI:

```powershell
python -m tools.lp_resolver.gui
```

or:

```powershell
python -m tools.lp_resolver --gui
```

Main flow:

1. Set `MO2 Root`, `Profile Path`, `Output Dir`.
2. Click `Scan`.
3. Review conflicts in the table.
4. Set per-conflict decision:
   - `Ignore`
   - `Keep Highest Priority LP`
   - `Choose Specific LP Entry`
   - `Disable LP`
5. Save/load decisions (`resolver_decisions.json`).
6. Export patch mod (`mods/LP_ConflictPatch` by default).

Quick action buttons:

- `Disable LP For All Overlaps`
- `Keep Highest For All Duplicates`

## 3. Patch Output

Patch export writes:

- `mods/<PatchName>/LightPlacer/<PatchName>/resolved.json`
- `mods/<PatchName>/resolver_decisions.json`
- `mods/<PatchName>/resolver_report.md`

The exporter applies only chosen decisions and leaves source mods untouched.

## 4. Packaging For End Users

Install dependencies:

```powershell
python -m pip install -r tools\lp_resolver\requirements.txt
```

Build app bundle:

```powershell
powershell -ExecutionPolicy Bypass -File tools\lp_resolver\build_windows.ps1 -Clean
```

Result:

- `dist\lp_resolver_app\LPConflictResolver\LPConflictResolver.exe`

Optional installer:

- Open `tools\lp_resolver\LPResolver.iss` in Inno Setup.
- Set `SourceDir` to your built bundle path.
- Compile installer.

## 5. Recommended Distribution Workflow

1. Build with `build_windows.ps1`.
2. Smoke-test the EXE on a clean machine (no Python installed).
3. Publish:
   - portable zip (`LPConflictResolver` folder)
   - setup exe (Inno Setup output)
4. Include a short "Run from MO2 executable list" guide for users.
