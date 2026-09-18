# Raw trace retention update: 18 September 2026

At the user's request, the source ETL and its verified archive copy were
deleted for each of the original six culling runs. This freed 248.98 GiB
for the later matched-pose head/baseline investigation.

| Label                | Run                                    |
| -------------------- | -------------------------------------- |
| Balanced 1           | gameft-sw-20260916T152832143Z-3828ffef |
| Performance          | gameft-sw-20260916T154518977Z-a6e09be6 |
| Legacy               | gameft-sw-20260916T155929972Z-2a6a7693 |
| Balanced 2 uncertain | gameft-sw-20260916T161424154Z-401a5499 |
| Balanced 3           | gameft-sw-20260916T163602908Z-a26f2ef1 |
| Balanced 4           | gameft-sw-20260916T165146902Z-36b2c165 |

Only the twelve identified ETL files were removed. Existing WPA exports,
derived analysis, comparison tables, fpsVR recordings and provenance
receipts remain. The later Legacy 2 and depth-off traces were retained,
as were all six new matched-pose head/baseline traces.

The exact paths, previously verified ETL hashes, byte counts and hashes
of 26 preserved key reports/summaries are recorded locally in
`build/bisect/measurements/depth-culling-wpr-20260916/raw-etl-deletion-20260918.json`.
Those preserved files passed the post-deletion hash check. Historical ETL
archive receipts document the original verification; their paths no
longer imply that these twelve raw files are available.
