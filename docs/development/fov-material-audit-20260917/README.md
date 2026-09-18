# FOV+TAA history and material cost

This follow-up audits retained settings for 37 runs and reuses the existing
matched-window WPR material classification. It introduces no measurement,
rendering change or recalculation of fpsVR timing windows. Earlier numbered
ledgers remain unchanged.

The user-selected canonical baseline is
`gameft-sw-20260915T183826985Z`: renderer base
`190c28a39a52c2bace475d1143a124c5893ac741`, compiled diagnostics backport
`a1a11fe0d722fd6dbc18315a023701e0ec4a84de`. Its six saves completed with
WPR coverage and verified DLL/PDB provenance. FOV+TAA was On / centre 0.30 /
outer 0.70 before and after. The initial audit found one complete run;
three further baseline repeats completed on September 17 and are retained
in the [repeat comparison](../baseline-repeat-comparison-20260917/README.md).
The original run remains the primary comparison
reference; the material-only 0.30 comparison below uses the later
native-hook run as an explicitly separate reference, because that build
contains the material guard under investigation.

## What settings were actually recorded?

The two centre fields are different profiles. With FOV+TAA active,
`periphery_taa_center_area` selects the centre and `foveatedCenterArea`
is the inactive saved **FOV Only Visible Scale**. Do not classify a run's
active centre from that saved FOV-only value.

The September 17 reassessment reread all sixteen culling before/after
receipts. Every one reports `periphery_taa_enable=true`,
`periphery_taa_center_area=0.6000000238418579` and outer scale approximately
0.70. Both fields happen to be 0.60 in those receipts. The culling labels
below therefore remain correct. In contrast, today's three baseline
repeats have an enabled FOV+TAA centre of 0.30 and an inactive FOV-only
value of 0.60. The earlier chat description of that latter difference as
an active vendor-centre mismatch is withdrawn.

The exact receipt paths, SHA-256 hashes, raw field values and production
source references are in [profile-reassessment.json](profile-reassessment.json).
The original 37-run audit and historical ledgers remain unchanged.

| Runs                                                                               | FOV+TAA    | Active centre setting | Outer scale | Evidence                                                        |
| ---------------------------------------------------------------------------------- | ---------- | --------------------: | ----------: | --------------------------------------------------------------- |
| Original traced baseline, initial/repeated main-VR diagnostics                     | On         |                  0.30 |        0.70 | Before and after                                                |
| PR65                                                                               | On         |                  0.30 |        0.70 | Before only; after response lacks settings                      |
| PR92, PR93                                                                         | On         |                  0.30 |        0.70 | Before and after                                                |
| Partial PR93 reversal and its repeat                                               | On         |                  0.30 |        0.70 | Before and after                                                |
| Native stereo-hook correction                                                      | On         |                  0.30 |        0.70 | Before and after                                                |
| All eight `503fbfc` culling runs: four Balanced, Performance, two Legacy, disabled | On         |                  0.60 |        0.70 | Before and after                                                |
| Three RC166 repeats                                                                | On         |                  0.30 |        0.70 | All 54 existing late legacy health receipts                     |
| Three complete material repeats                                                    | On         |                  0.30 |        0.70 | Before and after                                                |
| Interrupted material repeat                                                        | On         |                  0.30 |        0.70 | Before; after missing                                           |
| Three September 17 baseline repeats                                                | On         |                  0.30 |        0.70 | Before and after; inactive FOV-only value is 0.60               |
| Thirteen earlier three-save `game-ft` runs                                         | Unverified |            Unverified |  Unverified | No full snapshot or exposed late FOV fields in audited receipts |

The [audit data](audit.json) lists every run, exact producer identity where
retained, source paths/hashes and field values. The legacy RC166 status
receipts expose the relevant requested Upscaling settings and active FOV
profile even though those builds lack the modern full-feature snapshot.
This resolves that narrow settings question; it does not recover all feature
settings or modern lifecycle health.

The first observed 0.60 group is the September 16 culling campaign. It
returns to 0.30 by the RC166 campaign. The records do not identify who or
what changed the setting. All eight culling runs used 0.60, so this finding
does not invalidate their internal culling comparison. It does confound
cross-group comparisons between those runs and RC166/current repeats.

The earlier traced baseline/PR92/PR93/partial-reversal/native-hook
comparisons do not have this particular centre mismatch. PR65 has weaker
end-of-run settings evidence. None of these snapshots proves settings
through every instant of a hold. Older three-save `game-ft` measurements
also remain separate from WPR-active `gameft-sw` comparisons.

## Relative WPR evidence for the material change

Use the earlier native-hook correction run as a retained **0.30-centre**
reference, instead of the later 0.60 culling runs. It compiled
`933a4e2540f962ddc3d2ce656523d1f8c5982ef4`; the current candidate compiled
`6588831aabf472dff76340ed58aaf07075fccacd`. The table uses the same expanded
guard union, includes the new rejection helper, and counts each sample
once. Current values are per-save medians of the three complete repeats.

| Save    | Earlier guard ms/frame | Current guard ms/frame |   Delta | Relative delta | Share of sampled render CPU, earlier → current |
| ------- | ---------------------: | ---------------------: | ------: | -------------: | ---------------------------------------------: |
| DLAA 08 |                 0.1231 |                 0.1182 | -0.0050 |          -4.0% |                                0.842% → 0.829% |
| DLAA 09 |                 0.1189 |                 0.0865 | -0.0325 |         -27.3% |                                1.141% → 0.998% |
| DLAA 10 |                 0.1283 |                 0.1072 | -0.0211 |         -16.4% |                                1.108% → 1.018% |
| DLSS 11 |                 0.0436 |                 0.0550 | +0.0113 |         +26.0% |                                0.475% → 0.622% |
| DLSS 12 |                 0.0280 |                 0.0545 | +0.0265 |         +94.8% |                                0.295% → 0.574% |
| DLSS 13 |                 0.1092 |                 0.1490 | +0.0398 |         +36.4% |                                0.669% → 0.883% |

The previously quoted approximately **0.135 ms** is present in the partial
PR93-reversal repeat's **Save 10**: 0.13512 ms/frame. Current Save 10 has a
0.10725 median, a descriptive reduction of 0.02787 ms/frame (20.6%). That
single result does not establish a general material saving. In particular,
the cost is higher in every DLSS save against both earlier 0.30 references.
Both the per-frame metric and share of sampled render CPU show that split.

These are measured workload costs, not isolated per-call microbenchmarks.
The old/new builds contain other changes, frame cadence differs, and WPR
records samples rather than material-check invocation counts. Tens of
matching samples per ten-second window also limit precision. Dividing by
total CPU time does not remove those confounds. The evidence supports
**no consistent material-cost reduction demonstrated**, not a claim that
the last commit caused either the lower DLAA values or higher DLSS values.

The code change outlined the cold warning body; it retained the protected
material reads and decisions. No current tail samples matched the outlined
warning helper. There is no evidence that the roughly 0.135 ms cost was
eliminated. A source-isolated, settings-matched repeat is needed to attribute
a smaller saving specifically to that commit.

## Keeping future comparisons consistent

The [protocol](../gameft-sw.md#fixed-fovtaa-settings-for-this-performance-campaign)
now pins On / centre 0.30 / outer 0.70 for this performance campaign and
requires live pre-load verification plus after-run checking. No game setting
was changed. Automated enforcement in the wrapper remains separate work;
the current wrapper only records the snapshots, so this is an explicit
operator requirement rather than a claim of an implemented automatic gate.

Local validation reconstructs all audited fields from their retained source
receipts and checks the material arithmetic against the existing CSV:

```powershell
python build/cpu-burst-diagnostics/verify-fov-material-audit.py
```

The source records remain in their original locations and existing ledgers;
this audit does not overwrite or relabel historical measurements.

Publication note: machine-specific paths use portable root labels. Historical
scheduler flags retain their original policy; see the additive
[10 ms reassessment and complete ledger record](../gameft-publication-20260918/README.md).
