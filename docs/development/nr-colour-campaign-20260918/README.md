# Preserved NR colour measurements

Start with the [consolidated investigation](../nr-colour-hmd-investigation-20260918.md)
for decisions, key measurements, image findings and remaining limitations.
This directory contains the complete exported measurement/review payloads
from the 23 retained evidence folders listed in archive-index.json.

It records 2,907 payloads and indexes 168 additional raw capture manifests.
The run shards total 113,201,863 bytes (about 108 MiB); the largest is under
16 MiB. They are split by evidence folder so no Git object approaches the
100 MiB file limit. Original PNGs and binary build artifacts remain local.

## Reading the records

Each UTF-8 JSONL line is one original source document, with:

-   source.path: its worktree-relative original pathname.
-   source.bytes and source.sha256: the original file identity.
-   format: how value represents that file.
-   value: the complete original parsed JSON or text, except explicitly
    summarized continuous recording arrays.
-   sourceNotes, when present: omitted trace-array counts or the original
    JSON parse error.

Formats:

| Format                        | Meaning                                                                                                                     |
| ----------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| complete-json                 | Every field in the original JSON value is retained.                                                                         |
| complete-text                 | Complete CSV, Markdown, analysis/capture source text or other selected text; numeric CSV cells remain their original text.  |
| recording-metadata-and-events | Recording metadata and activity events retained; steps/trackingSamples arrays stay local, with their counts in sourceNotes. |
| unparsed-json-text            | Original incomplete/invalid JSON retained as text with its parse error; no successful review or measurement is inferred.    |

The source inventory maps every selected source file to its shard and
one-based record number. Raw capture manifests have disposition
local-raw-capture-manifest and their size/hash, without embedding the raw
manifest. Original-image identities remain available in preserved artifact
descriptors, indices and review mappings. Embedded paths are historical
provenance and are not portable filesystem destinations.

The one unparsed JSON file is the follow-up's preliminary
live-sequences.json. Its complete raw text and parse error are retained;
the final report and result tables remain separate authoritative records.

Example: extract the dark campaign's accepted early comparison summary.
Run this from the repository root; no image packages or running game are needed.

```python
import json
from pathlib import Path

root = Path("docs/development/nr-colour-campaign-20260918")
shard = root / "runs/nr-dark-two-characters-20260917T081914Z.jsonl"
with shard.open(encoding="utf-8") as stream:
    for line in stream:
        record = json.loads(line)
        if record["source"]["path"].endswith("/results/qualified-summary.json"):
            print(json.dumps(record["value"], indent=2))
```

To inspect a per-frame table, select results/regions.json or
analysis/private/regions.json instead. For detailed older visual reviews,
select the saved blind-review, analysis/review or sealed-reviews records.
The Dragonsreach follow-up REPORT.md and same-frame investigation REPORT.md
are complete-text records in their respective shards.

## Interpretation and coverage

A stored row is not necessarily a valid comparison. Preserve its exclusions,
condition, frame, eye, region, units and reference. Follow final reports and
qualified summaries rather than unfiltered diagnostic tables. In particular:

-   The dark run's results/summary.json and baseline.json are unfiltered.
    Use comparison-exclusions.json, early-baseline.json,
    early-source-relative-effects.json and qualified-summary.json.
-   The first offline camera audit in the Dragonsreach follow-up is
    superseded by offline-audit-v2.json; both are historical records.
-   Some original reviews were blinded; the user later cancelled that
    requirement. An interrupted reviewer output is not a completed scorecard.
-   Record counts include receipts, analysis programs, historical copies and
    reanalysis. They do not count independent frames or replications.
-   Encoded HMD luma, linear luminance and private texture-domain values are
    distinct quantities. Keep their original definitions and provenance.
-   A clean attribution check does not remove motion, menu or illumination
    confounds and does not prove useful added neural detail.

This export adds no new measurements or image transformations. The archived
scripts are provenance, not a maintained live runner; do not execute them
to recreate a game session. All source files were read in place. Their
bytes, hashes and exported values were verified again after export.

The earlier null-HMD records are linked from the consolidated report and
indexed as committed narrative reports. Their old raw evidence path was
unavailable in this workspace; this snapshot does not manufacture its
missing raw rows. The physical-HMD archive retains all selected structured
and text files in the 23 evidence folders, with the stated trace and
raw-manifest exclusions.

## Evidence folders

| Evidence                                       | JSONL payload                                                                                                     |
| ---------------------------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| Bright slider, two characters                  | [nr-bright-two-characters-20260917T0723Z](runs/nr-bright-two-characters-20260917T0723Z.jsonl)                     |
| Dragonsreach follow-up and old-image audit     | [nr-colour-followup-20260916T214643Z](runs/nr-colour-followup-20260916T214643Z.jsonl)                             |
| Dark slider, one visible character             | [nr-dark-two-characters-20260917T081914Z](runs/nr-dark-two-characters-20260917T081914Z.jsonl)                     |
| Corrected detail-strength comparison           | [nr-detail-character-20260916T201940Z](runs/nr-detail-character-20260916T201940Z.jsonl)                           |
| Detail admission / schema refresh              | [nr-detail-live-20260916-admission](runs/nr-detail-live-20260916-admission.jsonl)                                 |
| Detail scout / framing correction              | [nr-detail-live-20260916T2012Z](runs/nr-detail-live-20260916T2012Z.jsonl)                                         |
| Brighter attempt interrupted by scene move     | [nr-hmd-brighter-20260916T125450Z](runs/nr-hmd-brighter-20260916T125450Z.jsonl)                                   |
| Brighter Raw/Managed/Preserve comparison       | [nr-hmd-brighter-20260916T125450Z-restart](runs/nr-hmd-brighter-20260916T125450Z-restart.jsonl)                   |
| Indoor fixed baseline                          | [nr-hmd-fixed-campaign-20260916T1100Z](runs/nr-hmd-fixed-campaign-20260916T1100Z.jsonl)                           |
| Fixed-pose prerequisite probe                  | [nr-hmd-fixedpose-20260916T1054Z](runs/nr-hmd-fixedpose-20260916T1054Z.jsonl)                                     |
| Lower-sunlight baseline                        | [nr-hmd-lower-sun-20260916T115411Z](runs/nr-hmd-lower-sun-20260916T115411Z.jsonl)                                 |
| QASmoke stale-reference rejection              | [nr-hmd-new-scene-20260916T121535Z](runs/nr-hmd-new-scene-20260916T121535Z.jsonl)                                 |
| QASmoke fresh-reference retry                  | [nr-hmd-new-scene-20260916T121535Z-fresh-reference](runs/nr-hmd-new-scene-20260916T121535Z-fresh-reference.jsonl) |
| Outdoor tracked baseline                       | [nr-hmd-outdoor-20260916T111347Z](runs/nr-hmd-outdoor-20260916T111347Z.jsonl)                                     |
| Outdoor fixed baseline / journal               | [nr-hmd-outdoor-fixed-20260916T112605Z](runs/nr-hmd-outdoor-fixed-20260916T112605Z.jsonl)                         |
| Outdoor float32-aware frozen tolerance         | [nr-hmd-outdoor-fixed-20260916T112605Z-precision](runs/nr-hmd-outdoor-fixed-20260916T112605Z-precision.jsonl)     |
| Outdoor over-strict tolerance rejection        | [nr-hmd-outdoor-fixed-20260916T112605Z-retry](runs/nr-hmd-outdoor-fixed-20260916T112605Z-retry.jsonl)             |
| First capture / stale camera evidence          | [nr-hmd-readiness-20260916T090800Z](runs/nr-hmd-readiness-20260916T090800Z.jsonl)                                 |
| Camera-repaired baseline retest                | [nr-hmd-retest-20260916T1015Z](runs/nr-hmd-retest-20260916T1015Z.jsonl)                                           |
| Second indoor scene / journal                  | [nr-hmd-scene2-20260916T1039Z](runs/nr-hmd-scene2-20260916T1039Z.jsonl)                                           |
| Slider admission and preliminary images        | [nr-lighting-live-20260917](runs/nr-lighting-live-20260917.jsonl)                                                 |
| Packed-rounding corrected HMD comparison       | [nr-packed-rounding-live-20260917T023044Z](runs/nr-packed-rounding-live-20260917T023044Z.jsonl)                   |
| Private same-frame bias and WARP investigation | [nr-same-frame-bias-20260916T233000Z](runs/nr-same-frame-bias-20260916T233000Z.jsonl)                             |

## Validation

validation.json records complete payload-to-source equality and exact
coverage counts. archive-index.json contains every shard hash; the source
inventory carries the original source-file hashes. A later format-only
change to the small index requires updating its seal, not rerunning
the measurements.

The archival checks do not recapture images, rebuild shaders, change the
AIO, infer missing values, or reinterpret failed comparisons as passes.
