# Measurement corpus

Each controlled run is described by a JSON record validated against [`../schemas/measurement-run.schema.json`](../schemas/measurement-run.schema.json). Start with [`measurement-run.template.json`](./measurement-run.template.json).

NVIDIA contributors should start with [`nvidia-run.template.json`](./nvidia-run.template.json) and follow the complete [contribution guide](../CONTRIBUTING.md). [`corpus-index.json`](./corpus-index.json) is the generated searchable inventory; do not edit it manually.

## Identity and layout

Use a stable ID such as:

`20260817-amd-svr-ovr-null-scene01-baseline-r01`

An NVIDIA example is:

`20260818-nvidia-svr-ovr-physical-dragonsreach-balanced-r01`

When curated records are added, group them by date or campaign without changing their `runId`. Refer to baseline and candidate run IDs explicitly; do not infer pairing from filenames.

## Artifact policy

The small JSON record, manifest, settings snapshot, profiler export, and textual summaries are candidates for source control after review. Lossless stereo sequences, videos, RenderDoc captures, dumps, and other large artifacts remain outside Git unless a deliberate Git LFS or release-asset policy is adopted.

An external artifact reference must include:

- a stable relative path, managed URI, or campaign-local locator;
- media/type and byte size when known;
- SHA-256 for immutable evidence; and
- retention state and access notes.

Do not commit machine-specific absolute paths as the only locator. They may appear as a local convenience field alongside a stable locator.

## Run lifecycle

1. Create the record as `planned`.
2. Populate provenance before changing the candidate.
3. Set `incomplete` if the process stops before all intended passes finish.
4. Set `invalid` and retain the reasons when a validity gate fails.
5. Set `valid` only when the protocol gates pass.
6. Add conclusions only within the recorded hardware, lane, scene, and control scope.

Invalid runs are useful evidence about the harness and should not be silently deleted. They cannot enter preset frontier comparisons.

Schema version 2 permits an explicitly missing settings digest only for `incomplete` or `invalid` records with `validity.accepted=false` and a non-empty `provenanceGaps` list. This supports honest retrospective indexing; it does not relax the provenance gate for new evidence.

## Separation of passes

Profiler timing and screenshot capture are separate passes connected by candidate, scene, pose script, settings digest, and runtime lane. Their records may share one run ID only when the schema's `passes` entries preserve that separation and all provenance is unchanged.

## Retrospective AMD import

The [`retrospective/20260817-amd-svr-ovr-null`](./retrospective/20260817-amd-svr-ovr-null) collection normalizes every archived runner record from the 17 August AMD campaign. It contains per-phase timing distributions or visual capture completeness plus immutable archive locators and source-record SHA-256 values.

The original runners accepted 70 of those records and rejected 14. The retrospective canonical records deliberately classify the 70 as `incomplete`, because the archived runner format did not serialize the exact complete settings snapshot digest or dependency identity. The source-level acceptance rationale remains preserved, but these records cannot enter a cross-vendor frontier comparison. The earlier seven fully provenanced IBL records remain `valid`; the Honeyside proof remains `invalid`.

[`import-preset-calibration-archive.ps1`](../../../../tools/import-preset-calibration-archive.ps1) performs the deterministic one-time archive conversion. It is not needed by contributors or CI; public validation operates entirely on the committed normalized records.
