# VR render-scale ledger index

The durable measurement record is a sequence of immutable numbered CSV
snapshots. Each future snapshot contains only its selected baselines and
the measurements belonging to the PR that commits it. Every finalized
measurement advances the global sequence, including another run on the
same PR. Earlier snapshots stay unchanged. See the
[reporting contract](vr-render-scale-comparison-reporting.md).

## Retained snapshots

| Version | File                                                                          |                                                                    Size | Contents                                                                                                                                                     |
| ------- | ----------------------------------------------------------------------------- | ----------------------------------------------------------------------: | ------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 0001    | [History 1](vr-render-scale-ledger-0001-history.csv)                          |                                           83,486,866 bytes / 79.619 MiB | Oldest retained runs plus intermediate runs selected to balance the archives; 22 run columns                                                                 |
| 0002    | [History 2](vr-render-scale-ledger-0002-history.csv)                          |                                           83,607,079 bytes / 79.734 MiB | Remaining intermediate PR66/PR73 runs; 4 run columns                                                                                                         |
| 0003    | [PR73 and baselines](vr-render-scale-ledger-0003-pr73.csv)                    |                                           72,209,582 bytes / 68.864 MiB | Both PR65 baseline repeats, PR66 reference, latest PR73 measurement; 4 run columns                                                                           |
| 0004    | [Depth-culling investigation](vr-render-scale-ledger-0004-investigation.csv)  | See [coverage receipt](depth-culling-comparison-20260916/coverage.json) | Eight same-build gameft-sw runs; 48 saves; complete summaries, producer final records, settings, provenance and derived WPR analysis                         |
| 0005    | [RC166 and culling comparison](vr-render-scale-ledger-0005-investigation.csv) |         See [coverage receipt](rc166-comparison-20260916/coverage.json) | All eight prior runs plus three RC166 repeats; 66 saves; complete legacy receipts, provenance, matched WPR analysis and explicit health/coverage limitations |
| 0006    | [Material comparison, part 1](vr-render-scale-ledger-0006-investigation.csv)  |      See [coverage receipt](material-comparison-20260917/coverage.json) | Selected Balanced 1/3/4 cells, original traced reference, current R1/R2; complete receipts and WPR analysis                                                  |
| 0007    | [Material comparison, part 2](vr-render-scale-ledger-0007-investigation.csv)  |      See [coverage receipt](material-comparison-20260917/coverage.json) | Current interrupted repeat and R3; missing Save 13 explicit; complete campaign comparisons/settings audit                                                    |
| 0008    | [Grass COC investigation](vr-render-scale-ledger-0008-investigation.csv)      |                                           57,070,396 bytes / 54.427 MiB | Three same-process COC runs, all 65 transitions, two exact historical reference columns, complete summaries and clean shutdown                               |

The archives differ by 120,213 bytes. The original three are plain CSV files below
100 MiB. Their column headers retain exact run and compiled-source
identities; each has all 1,228 metric rows. Historical files are balanced
by complete run columns, so intermediate dates can occur in either file.

Snapshot `0003` pins PR65 sources `348803c18` and `7c8e3e656`, PR66 source
`a09e1cc77`, and PR73 source `554e484e3` from
`renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z`. The older PR73
comparison source `269bded15` is retained in `0002`.

The next finalized measurement uses `0009-pr<PR number>`. If one PR's
complete evidence exceeds the file limit, use consecutive numbers with
that PR identity and list its parts together here. Baselines may recur
in later snapshots, but their copied cells must remain exact.

Snapshot `0004` belongs to `perf/cpu-dlss-regression-20260916`, which has no
assigned PR. Its `investigation` suffix records that fact without inventing
a PR identity. It uses eight run columns, scalar timing rows and complete
JSON detail rows; it is a gameft-sw diagnostic snapshot, not a replay of the
older render-scale tuning assay. The [handover](depth-culling-comparison-20260916/README.md)
defines its internal Balanced 1 reference separately from the historical
cross-build baseline. Decode detail cells with JSON; increase the CSV
reader field limit to the ledger byte length. The
[coverage receipt](depth-culling-comparison-20260916/coverage.json) maps
every source object and verifies exact reconstruction. Raw traces remain
local. Historical snapshots `0001`–`0003` are unchanged.
The tuning comparison wrapper discovers `prNUMBER` and `history`
snapshots; it does not interpret this investigation schema as a tuning
assay. Use the committed `verify.py` for its coverage audit.

Snapshot `0005` extends the same investigation with the three user-labeled
RC166 repeats. It preserves every `0004` cell unchanged and adds the exact
`2eef86720` producer identity, original vendor-library hashes, all legacy
health receipts and derived CPU stack/ready/wait results. See the
[comparison and limitations](rc166-comparison-20260916/README.md) and run
`python docs/development/rc166-comparison-20260916/verify.py` for the portable
reconstruction, timing and historical-cell audit. These are whole-build
comparisons, not an isolated culling or vendor-library experiment.

Snapshots `0006` and `0007` partition the same investigation's material
candidate campaign into disjoint run columns below 100 MiB each. Together
they retain 23 current windows, four selected reference runs, all available
receipts, raw interrupted placeholders with explicit invalidity, and the
matched timing/WPR comparison. Earlier numbered files and copied reference
cells are unchanged. The [assessment](material-comparison-20260917/README.md)
records the active FOV+TAA centre mismatch and the absence of a consistent
material saving. Run `python docs/development/material-comparison-20260917/verify.py`
to audit both partitions, receipt reconstruction, selected historical cells,
window validity, late profiles and comparison arithmetic.

Snapshot `0008` records the direct main-VR grass implementation assay on
measured source `3baaf91b90416ad25d067cdb26c34ce293cf7a5e`; no PR number
was assigned. The original main-VR snapshot `0005` is retained byte-for-byte as `0008`
to preserve the distinct NR snapshots `0005` through `0007`. Its
`investigation` suffix follows snapshot `0004`.
The [report](grass-coc-20260926/README.md) separates all 65 strict successes
from raw aggregate PASS / FAIL / FAIL and the two proven native-path gate
contract mismatches. The complete summaries, exact requests, per-transition
timings, comparisons and shutdown receipt are in structured detail cells.
The two historical columns are copied exactly from `0001`; snapshots
`0001`–`0004` remain unchanged. The [coverage receipt](grass-coc-20260926/coverage.json)
records exact JSON reconstruction, scalar timing coverage, historical hashes
and all measured source identities. This is a direct COC scenario format,
not the tuning wrapper's fixed-matrix worker protocol. Raw traces stay local.

## Migration verification

The original 239,169,839-byte ledger had SHA-256
`b9c0f6326174c5d4524b8a2dfdc2cf1754ead3386745b31dc4d048305943ae21`.
All 30 run columns were assigned exactly once. Reassembling the three CSVs
by their original column indices reproduced all 38,068 data cells exactly,
including structured JSON, zero, false, null and empty values. No timing,
failure, retry, recovery or historical cell was discarded.

The [migration receipt](vr-render-scale-ledger-migration-20260911.json)
records each file's byte size, SHA-256, source column positions and complete
run headers. The original monolithic CSV remains in local evidence.
Reports written before this migration may name its historical path;
their run columns are now in the numbered files indexed here.

## Compare retained history

The reporting wrapper defaults to the highest numbered snapshot. Pin the
snapshot explicitly when reproducing a report. For the latest PR73 versus
its previous measurement, add the historical partition:

```powershell
python tools/compare-render-scale-ledger.py --ledger docs/development/vr-render-scale-ledger-0003-pr73.csv --ledger-archive docs/development/vr-render-scale-ledger-0002-history.csv --toolkit-root <toolkit> --baseline-root <previous-run> --candidate-root <latest-run> --output-root <local-comparison>
```

This reads the selected files without changing them. Select disjoint run
partitions when using multiple inputs; do not supply repeated baseline
columns from different snapshots to the same audit.

## September 15 graphics ownership implementation

The [ownership review](graphics-context-ownership-review-20260915.md) records
an offline correction and its validation limits. No new finalized runtime
measurement is added by that correction; existing numbered snapshots remain
immutable. Exact-build COC and performance results must receive a new
numbered snapshot when measured and finalized.
