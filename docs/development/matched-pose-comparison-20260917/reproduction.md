# Reproduction and validation

The preserved inputs and local analysis scripts are under
`build/bisect/measurements/` and `build/cpu-burst-diagnostics/` in
`${CSX_ROOT}`. Script identities are listed
in [analysis-scripts.json](analysis-scripts.json). Raw traces and WPA CSV
exports remain local; the report contains compact derived evidence.

These commands were run for this cohort:

```powershell
python -u ./build/cpu-burst-diagnostics/archive-matched-pose-20260917.py --archive-root '${LOG_ARCHIVE}' --small-only
python -u ./build/cpu-burst-diagnostics/archive-matched-pose-20260917.py --archive-root '${LOG_ARCHIVE}' --run 'Head R1'
python -u ./build/cpu-burst-diagnostics/archive-matched-pose-20260917.py --archive-root '${LOG_ARCHIVE}'
python -u ./build/cpu-burst-diagnostics/matched-pose-wpr-20260917.py --run 'Head R1'
python -u ./build/cpu-burst-diagnostics/complete-matched-pose-wpr-20260918.py
python ./build/cpu-burst-diagnostics/report-matched-pose-20260917.py
python ./build/cpu-burst-diagnostics/report-matched-pose-wpr-20260918.py
python ./build/cpu-burst-diagnostics/report-matched-pose-counters-20260918.py
python ./build/cpu-burst-diagnostics/verify-matched-pose-20260918.py
pwsh ./tools/git.ps1 diff --check
```

The serial completion driver invokes the same per-run adapter for Head
R2/R3 and Baseline R1/R2/R3. It then reuses the saved classifier and context
analysis. Completed exports were reused only with matching ETL identity;
no benchmark was replayed. Preliminary core analyses were generated for
completed traces while the next export ran, then reused in finalization.

WPT was invoked from
`${PROGRAMFILES_X86}/Windows Kits/10/Windows Performance Toolkit`:

```text
xperf.exe -i <verified ETL> -o <trace statistics> -a tracestats -detail stack -timespan actual
xperf.exe -i <verified ETL> -o <image details> -a process -image detail
wpaexporter.exe -i <verified ETL> -symbols -profile <sampled profile> -outputfolder <analysis> -prefix current-
wpaexporter.exe -i <verified ETL> -symbols -profile <render-thread precise profile> -outputfolder <analysis> -prefix render-
```

Exact expanded commands, exit codes and logs are retained in each run's
`stack-wait/analysis` folder. The matching run-local PDB and retained symbol
cache were supplied explicitly. Named symbols can be absent or inlined;
these traces do not supply private Skyrim or driver function names.

The first Head R1 adapter attempt failed after CPU export because its
saved pickle referenced the original `__main__.fresh_thread` factory.
Restoring that factory in the offline adapter allowed the same exported
data to be reused. No measurement rule, classifier or window changed.
The initial traceback and resumed pipeline logs are preserved locally.
WPA also warned about an absent optional `MyPresets.wpaPresets` file;
the explicitly supplied profiles exported successfully with exit code 0.

The saved strict scheduler summarizer reported coverage errors for Head
R1 Save 12, Head R2 Save 12 and Head R3 Save 13. Final analysis retains
all their raw values and flags under the original 5 ms tolerance. This is
33 passing windows and three provisional windows, not 36 scheduler passes.
Those errors do not invalidate the independent fpsVR mean reconstruction
or change which samples belong to the measured holds.

The WPR pipeline ran from 23:03:41 UTC to 23:47:59 UTC on 17 September,
approximately 44 minutes 17 seconds including the adapter recovery. This
does not include the earlier archival or subsequent report preparation.

Verification reconstructs all 36 saved means, compares all preserved quick
summary fields, validates six PDB hashes and trace-quality receipts,
checks 108 late profiles, verifies common settings and module metadata,
and checks median/delta arithmetic and README mean tables. The final
[validation receipt](validation.json) records the retained coverage flags
and hashes of all published outputs.

Scoped formatting command:

```powershell
$reportFiles = @(
    Get-ChildItem docs/development/matched-pose-comparison-20260917 -File |
        ForEach-Object { 'docs/development/matched-pose-comparison-20260917/' + $_.Name }
)
$reportFiles += 'docs/development/depth-culling-comparison-20260916/raw-trace-retention.md'
pwsh ./tools/pre-commit.ps1 run --files @reportFiles
```

The analysis branch remained `perf/cpu-dlss-regression-20260916` at
`60179f5b5289eaf8d42ffe030425f063edf3c6eb`. Only the new comparison report
and culling retention note were added to the repository's visible changes.
Pre-existing working-tree changes were preserved. No commit or push was
performed, and no rendering implementation, build or deployment changed.
