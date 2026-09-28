# Task 3A Bannered Mare continuation, 2026-09-28

The current-session VR functional matrix passed. This extends the
[A1 lifetime check](nr-lifetime-isolation-20260928.md) in the same PID;
it does not replace independent fresh-process isolation or establish the
cause of the earlier device removals.

## Producer and scope

The user requested `coc WhiterunBanneredMare`, then continued Task 3A
testing in the existing Skyrim VR PID `27212`, port `8921`. The loaded
cell was verified as `WhiterunBanneredMare`, form `0x0001605E`.
The user chose to continue with the current view. Eligible actor counts
and view contents changed during the run; neither camera nor animation
was frozen. No installation, restart, build, commit or push occurred.

-   Build ID: `490462394cae63ac3f49cee6d3adca293ed8dfd202b71f66259655896c9030b7`.
-   Compiled source: `7c4effa2749bb8d3cf123818ad6cf06bdc03a7cf`, dirty digest
    `ff66918ffd95f7ab0a7662ad9f70d1058686871f98be724cef7241eef595cf73`.
-   Physical DLL: 31,065,600 bytes, SHA-256
    `48a133756b21b6c92e09e15c39c4adac0d29de72e8a49f0b56f1f7f14cd2ce21`.

The enabled AIO's physical DLL, adjacent manifest and preserved AIO receipt
were reverified. The exact selected MO2 profile had one enabled loose
provider and no Overwrite or unmanaged Data DLL shadow. The producer's
previous complete validation remains **208/208 CTests passed**; tests were
not rebuilt or repeated for this runtime-only continuation.

The fixture used DLSS K Quality, Render Scale on, FOV `0.95`, character
selection and experimental multi-ROI enabled, its default savings gate,
and `legacy_raw`. The initial FSR preference was retained. Lifetime/frame
evidence was enabled through the existing bridge-only capture switch.
No production implementation or performance policy changed in this run.

## Runtime checks

There were **67,281 additional successful native region evaluations** and
**27,311 successful stereo transactions**. Region evaluations are not
eye-frame counts when an eye splits. Final process totals were 90,479
evaluations and 38,910 successful stereo transactions. Failure, device
removal, quarantine, stereo failure, validation failure and reset failure
counters stayed zero. All 24 additional reset attempts succeeded.

| Transition | Receipt observation frame | Full retirement | Outcome |
| ---------- | ------------------------- | --------------- | ------- |
| A to B     | 107130                    | Not requested   | Passed  |
| B to C     | 109243                    | Succeeded       | Passed  |
| C to B     | 115026                    | Succeeded       | Passed  |
| B to A     | 117992                    | Not requested   | Passed  |
| A to C     | 121952                    | Succeeded       | Passed  |
| C to A     | 123491                    | Succeeded       | Passed  |

These are receipt observation frames, not claimed mutation timestamps;
`nr_configure` does not expose a separate mutation-frame field. Each route
was subsequently observed evaluating successfully. The B-to-A return
continued for an 83.867-second observation window before A-to-C. No fault
was reproduced. These windows are not GPU/CPU performance measurements.

Two disjoint regions per eye were captured and audited in every mode:
A frames 99965, 105585 and 123569; B frames 107248 and 115147; C frame 123328. One-region fallback also remained active when a split was not
eligible. Samples check the production descriptor roles, support/context
containment, exclusive disjoint output ownership, full allocation capacity,
preparation/execution identity, guide-grid mapping and private-output
commitment. C samples also retain jittered-output and native-reset rules.
The current run's sampled preparation bounds used the geometry fallback
while early GPU bounds were pending; this does not qualify every GPU-bound
split or ready/pending transition.

| Forced-empty mode | Start frame | End frame | Evaluation count at both endpoints |
| ----------------- | ----------- | --------- | ---------------------------------- |
| A                 | 102161      | 103340    | 40425                              |
| B                 | 116809      | 117476    | 66352                              |
| C                 | 111364      | 112678    | 58555                              |

Both eyes reported `empty_bypass` throughout the sampled endpoints. Native
evaluation deltas were zero, and authored selection subsequently resumed
in every mode. C was exercised with `renderscaleFov` both false and true;
A was exercised with `fovOnly` both false and true. These are functional
option checks, not a matched visual comparison of the two choices.

Capture was then disabled while NR remained active. Another **7,914**
successful evaluations occurred over a 105.506-second observation window,
with the complete diagnostic history unchanged at 28,202 total records.
The ring retained 64 entries with no diagnostic failure or frozen fault.
Its total includes the preceding A1 session. Colour input epochs remained
unchanged by capture toggles.

## Evidence, restoration and limitations

The offline audit covered **46 NR snapshots** and 39 distinct current-run
transactions: **21 successful ROI-contract samples**, **9 NoWork samples**,
and **9 inconclusive samples** lacking preparation at configuration
boundaries. One inherited transaction from the earlier A1 run was retained
but excluded from these counts. There were no audit violations. Missing
preparation remains inconclusive, not a pass or a renderer failure.

One native HMD stereo still completed with separate 1512x1680 left/right
PNG artifacts, `sdr_srgb`, explicit HMD source and rejected fallback.
Both committed files matched their receipt hashes and were visually
inspected. They show the Bannered Mare with an NPC near the bar; this
single changing scene is insufficient to judge NR effect, stereo fidelity
or equivalence to the pre-Task3A producer. No colour candidate was selected.

Original NR, character, FOV, colour and upscaling settings were restored
and compared against initial snapshots. NR and capture are off; B is
selected, character selection and multi-ROI are off, both optional NR FOV
flags are false, and the original FOV-off `0.3` mask is retained. DLSS K
Native AA, Render Scale off and FSR4 preference were restored through
successful public operation 4. Requested/configured/effective/stable and
persisted profiles matched; restoration did not save settings. The final
health receipt still reported PID `27212`, with no pending tasks.

Raw tool receipts, verified images, physical DLL proof, the game log,
`structural-audit.json` and `runtime-outcome.json` are preserved under
`build/validation/nr-task3a-bannered-mare-20260928-pid27212/`.
`audit_continuation.py` and `finalize_continuation.py` passed. The latter
retains corrected local evidence-path and schema-assumption diagnostics;
no raw receipts were lost or failures reclassified as passes.

Task 3A's production integration and local regression work are implemented
and validated. The current VR functional matrix now passes, including the
previously fault-associated mode sequence. **Full runtime qualification
remains incomplete**: the previous hang is unresolved, and this same-process
continuation cannot replace fresh A2/B1/B2 controls. Matched pre-Task3A
visual/performance comparisons, live SE/AE A+C, deliberately mixed-eye
NoWork and partial native/commit failure injection were not run. No claim
of performance neutrality or fixed driver behavior follows from this pass.
Task 3C and other context-policy experiments remain separate.

The next requested action is a manual fresh Skyrim process with the same
DLL for independent A2. Installation and restarts remain subject to the
user's explicit instruction. The current game was left running and healthy.
