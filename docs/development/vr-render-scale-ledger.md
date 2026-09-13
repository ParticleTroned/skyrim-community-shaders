# VR render-scale ledger index

The durable measurement record is a sequence of immutable numbered CSV
snapshots. Each future snapshot contains only its selected baselines and
the measurements belonging to the PR that commits it. Every finalized
measurement advances the global sequence, including another run on the
same PR. Earlier snapshots stay unchanged. See the
[reporting contract](vr-render-scale-comparison-reporting.md).

## Retained snapshots

| Version | File                                                           |                           Size | Contents                                                                                      |
| ------- | -------------------------------------------------------------- | -----------------------------: | --------------------------------------------------------------------------------------------- |
| 0001    | [History 1](vr-render-scale-ledger-0001-history.csv)           |  83,486,866 bytes / 79.619 MiB | Oldest retained runs plus intermediate runs selected to balance the archives; 22 run columns  |
| 0002    | [History 2](vr-render-scale-ledger-0002-history.csv)           |  83,607,079 bytes / 79.734 MiB | Remaining intermediate PR66/PR73 runs; 4 run columns                                          |
| 0003    | [PR73 and baselines](vr-render-scale-ledger-0003-pr73.csv)     |  72,209,582 bytes / 68.864 MiB | Both PR65 baseline repeats, PR66 reference, latest PR73 measurement; 4 run columns            |
| 0004    | [PR82 and PR73 baseline](vr-render-scale-ledger-0004-pr82.csv) |  62,247,890 bytes / 59.364 MiB | Latest PR73 baseline 554e484e3 and current main-VR e4cfd8f2f measured for PR82; 2 run columns |
| 0005    | [PR82 second run](vr-render-scale-ledger-0005-pr82.csv)        | 101,265,210 bytes / 96.574 MiB | Latest PR73 baseline and both e4cfd8f2f runs, with complete comparisons; 3 run columns        |

The archives differ by 120,213 bytes. All three are plain CSV files below
100 MiB. Their column headers retain exact run and compiled-source
identities; each has all 1,228 metric rows. Historical files are balanced
by complete run columns, so intermediate dates can occur in either file.

Snapshot `0003` pins PR65 sources `348803c18` and `7c8e3e656`, PR66 source
`a09e1cc77`, and PR73 source `554e484e3` from
`renderscale-tuning-nvidia-2026-09-11T17-09-55-165Z`. The older PR73
comparison source `269bded15` is retained in `0002`.

The next finalized measurement uses `0006-pr<PR number>`. If one PR's
complete evidence exceeds the file limit, use consecutive numbers with
that PR identity and list its parts together here. Baselines may recur
in later snapshots, but their copied cells must remain exact.

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

## September 13: PR82 measurement

Snapshot 0004 has only the last PR73 baseline and current main-VR test
AIO, with 1,263 metric rows. Both complete summaries and comparison
reconstruct exactly; all 1,056 timing cells pass audit. Original baseline
cells remain unchanged. Current source e4cfd8f2f is not PR82 head
76f9418ab. See the [report](nvidia-renderscale-tuning-pr82-e4cfd8f2f-20260913.md).

## September 13: second PR82 measurement

Snapshot 0005 retains the selected PR73 baseline and both current-build
runs, with 1,273 metric rows. Every cell from snapshot 0004 is unchanged;
all three summaries and both comparisons reconstruct exactly. Both
1,056-cell timing audits pass. See the [second-run report](nvidia-renderscale-tuning-pr82-e4cfd8f2f-20260913-repeat-192024.md).
