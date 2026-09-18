# Portable gameft publication: 18 September 2026

The baseline-repeat, FOV/settings audit and matched-pose reports are
preserved with their original measurements and explicit evidence gaps.
Complete numbered ledger snapshots: [vr-render-scale-ledger-0008-investigation.csv](../vr-render-scale-ledger-0008-investigation.csv), [vr-render-scale-ledger-0009-investigation.csv](../vr-render-scale-ledger-0009-investigation.csv).

The selected historical September 15 reference is retained separately from
three morning baseline repeats and the six evening matched-pose runs.
The evening three-head/three-baseline cohort is the matched-pose comparison;
do not pool its statistics with earlier poses or SSGI-disabled head runs.
Exact compiled source, dirty digest, renderer base, Build ID and DLL/PDB
identities remain in the source reports and complete ledger records.

## Current scheduler assessment

The current policy is `gameft-sw-scheduler-coverage-10ms-20260918` with an inclusive absolute
10 ms accounting tolerance over a 10,000 ms window. Applying it to the
unchanged recorded errors yields **24/24 baseline windows** and
**35/36 matched-pose windows**. Head R2 Save 12 remains outside tolerance
at +11.382900023627371 ms. Historical 5 ms flags remain historical;
the [reassessment](scheduler-reassessment.json) records both verdicts.
This changes no fpsVR timing, spike, render-health or trace-loss rule.

All six evening runs completed their holds and retained their evidence.
Baseline R1 and R3 after-settings receipts remain unavailable. Full-history
raw health failures and recovered stretch remain visible; terminal success
is not qualification. Matched pose is user-confirmed, not independently
measured; build/vendor differences and run-order effects remain limitations.
The reports do not reproduce a broad DLSS CPU-execution/GPU regression;
small DLAA execution differences and added native work remain descriptive
findings, not a formal improvement-or-neutral qualification verdict.

## Preservation and portability

No machine-specific absolute path is part of this publication. Logical
roots such as `${CSX_ROOT}`, `${AUTOMATION_ROOT}`, `${LOG_ARCHIVE}`,
`${USER_HOME}` and `${CODEX_HOME}` are resolved by the reader's machine.
Unknown roots use opaque `${LOCAL_PATH_...}` labels with a private local
map. Original source hashes identify the original bytes, before path
normalization. Original reports remain in the local pre-commit backup.
The [source inventory](preserved-sources.json) identifies byte-preserved,
SHA-256-verified measurement receipts and historical analysis programs in
the local log archive. The programs retain their exact historical bytes
there; they are not substituted into an active measurement toolchain.

The [coverage receipt](coverage.json) verifies 2247 complete source
records and 69397 numeric summary values, including all
per-save timing and settling fields, and reconstructs every ledger cell.
Earlier numbered ledgers remain byte-for-byte unchanged. Source receipts
preserve false, zero, null, empty arrays/objects, failed outcomes and errors.
The final column holds complete campaign comparisons and detailed derived
WPR/address evidence once. Raw ETLs and WPA exports remain local.

The existing six-run validation verdicts are preserved. Its output hashes
now identify portable report copies, while [original report hashes](original-report-hashes.json)
identify the earlier copies. Offline packaging validation is separate from
runtime qualification; no game, WPR, fpsVR, build or shader job was started.

Recheck the preserved publication without live tools:

```powershell
python docs/development/gameft-publication-20260918/verify.py
```
