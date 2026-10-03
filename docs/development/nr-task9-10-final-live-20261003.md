# Task 9/10 final live qualification of the calibration producer

## Decision and completion boundary

The replacement producer passes the outstanding bounded-candidate,
one-shot-search and capture-independent compact-control checks. The
current compact adapter is rejected for production: the stable 512 bucket
costs 5.95044 ms against a 5.10925 ms full-coordinate bracket (+16.46%).
This independently confirms the earlier negative 768-bucket result.
Task 10's tested-adapter evaluation is concluded with no promotion.

Task 9's implementation and tested runtime controls are qualified, but its
production cost-profile qualification is unsuccessful. No profile was
loaded, fitted from unjoined samples, or adopted. The specified unknown-cost
heuristic remains active. This does **not** mark Task 9's complete production
optimization goal achieved: exact per-key timing joins, provider residency,
held-out prediction errors and full perceptual qualification remain absent.
Repeating the same DLL and scene cannot manufacture those missing fields.

There is no newly identified CSX rendering defect requiring another AIO.
The host-controller correction is separate, on local automation `dev` as
`a2223b5`. No production defaults, shaders or runtime code changed in this
qualification commit. Task 11's existing ownership audit is unchanged.

## Exact producer and retained evidence

-   Build ID:
    `0eff0e9ceac16477e908573386f2ff834cf23d604dc0922357f81b7009eea489`.
-   Compiled source: `744815b7f7a8317fd93519bb2ad0453e8d19afbd`, dirty digest
    `60fddb97f0878afba02807ce5234902b480c29ee5f1766165e80687c4bf893a1`.
    The implementation was subsequently committed as `d79cafdcf`; this does
    not replace the original producer identity.
-   Physical DLL: 31,756,288 bytes, SHA-256
    `72a419a42240b28026a4a0dd52709c33cd7f15729695e58fc2b38f3376d73c67`.
-   Enabled provider: `CSX_AIO-main-vr-nr-NR-Task9Calibration-DevBench-20261003-0eff0e9ceac1`.
    Physical DLL, adjacent manifest and AIO receipt agree. The enabled loose
    provider inventory found no competing DLL, Overwrite DLL or unmanaged
    Data DLL. `dll-providers.json` preserves that proof.
-   Skyrim PID 78396, started `2026-10-03T21:57:30.4969931Z`.
-   Evidence root: `build/validation/nr-task9-10-final-live-20261003`.
    Raw evidence remains local. `window-audit.json`,
    `performance-evidence-audit.json` and `image-audit.json` index exact
    captures, histories, source frames, geometry, receipts and file hashes.

DevBench was on and Tracy off. The maintained bundled controller was the
sole live transport under the user's existing authorization; direct NR
tools were still absent from the callable catalog. Every measured call
passed the controller's performance-neutral guard. This checks the
standalone temporal probe, not the absence of all instrumentation.

## Stationary scene, then controlled movement

The initial Bannered Mare camera was preserved until the starting-view
tests finished. Before global `tai`, inspection proved that no console
reference was selected. Fenced output reported `All AI Processing is Off`.
All 20 actor positions matched that snapshot at the end of the starting-view
checks. AI-off is not a claim that lighting, animation or GPU input bytes
are identical. Three prominent NPCs were visible; detection could include
six eligible actors at the wider range.

The user-authorized free camera then provided a closer stationary view.
The supported `freecam`/`drive` API was used with ownership and rendered
image checks. No `tfc 1`, COC, save, game restart or DLL installation ran.
The identical 24-step camera path was executed once with full inputs and
once with compact inputs while AI remained off. Each produced a 32-frame
native stereo sequence; these are structural motion comparisons, not
frame-identical actor/lighting inputs or a blinded quality verdict.

AI was re-enabled with a separately verified fenced `tai` response. A
16-frame stereo sequence and actor-position changes prove the subsequent
AI-on phase. The final candidate/image check also ran with AI on.

## Task 9 controls and output qualification

The starting view enumerated 81 joint candidates with bounded search and
explicit coverage provenance. Separate keep, local-merge and split runs
each completed 60 unique increasing source frames. All records retained
exact key indices, native commits and output domains, without a loaded
profile. Cancellation of a 600-frame request retained 232 records.
Zero frame count, stale identity, missing key and empty profile were
rejected without mutation. Changing ROI geometry during a request stopped
it with `calibration_candidate_unavailable`, preserving the evidence.

A later 600-frame merged-image attempt stopped after five candidate
frames when current GPU coverage changed the admissible key. Its sixth
record is the explicit fallback/stop. That attempt is not a completed
600-frame calibration, and its subsequent still is not attributed to the
expired candidate. The guard was retained rather than relaxed to force
acceptance.

The final 60-frame merged-candidate run completed. Its eight-image stereo
sequence includes three exact candidate joins at source frames 123079,
123101 and 123122: both eyes show successful inference, private output and
outer pipeline composition, with two disjoint output domains in total.
The other five pairs were acquired after calibration ended and show the
ordinary fallback plan; they are not mislabeled candidate frames.

### One-shot inspection overhead

AI off, unchanged starting camera, C four-region configuration, capture
evidence off, 300 frames per window:

| Window             | NR stereo GPU self, ms | NR stereo CPU self, ms |
| ------------------ | ---------------------: | ---------------------: |
| Heuristic before   |              19.316013 |               4.783734 |
| One-shot inspected |              19.167868 |               4.654371 |
| Heuristic after    |              19.359629 |               4.595742 |

The GPU bracket midpoint is 19.337821 ms, with 0.23% drift. The inspected
window is 0.88% below that midpoint; this is not a claimed speedup.
Resource rebuild count remains 8 throughout. The earlier continuous
unqualified enumeration overhead is absent from this steady window.
Active calibration still performs explicit candidate work and is not a
production performance baseline.

The existing profiler retains capture IDs and ordered raw samples, but
does not expose an individual source-frame identity for each sample.
Candidate records do not independently supply native GPU cost. These two
streams must not be joined by array position or nearby wall-clock time.
There is therefore no valid per-key fit or held-out prediction-error
number from this run. Native provider residency is also unavailable.
The profile validator's qualification declarations were not set to true
to bypass those missing measurements.

## Task 10 compact result

With capture disabled, enabling compact plus an explicit reset admitted
256-sized storage in the starting view. This closes the former dependency
on image-evidence capture. Later current-source GPU bounds expanded to
832 pixels, beyond the largest 768 bucket, and correctly latched the full
fallback. That happened before capture was re-enabled, so it is not a
recurrence of the capture-toggle defect. The `compact-capture-off` timing
window is full-coordinate fallback evidence, not compact performance.

The closer stationary view retained 512x512 storage for both eyes. Its
300-frame compact window has resource rebuild count 34 before and after;
both adjacent full windows are also stable. Capture evidence is off for
all three measured windows.

| Window      | NR GPU self, ms | NR CPU self, ms | GPU p95, ms | GPU p99, ms |
| ----------- | --------------: | --------------: | ----------: | ----------: |
| Full before |        5.101072 |        1.556797 |    5.519861 |    5.720766 |
| Compact 512 |        5.950441 |        1.583813 |    6.434939 |    6.647077 |
| Full after  |        5.117421 |        1.571066 |    5.526700 |    5.674776 |

The full bracket drifts 0.32%. Compact adds 0.84119 ms / 16.46%, about
7.6% of an 11.11 ms / 90 Hz frame budget. CPU clocks remain separate.
These are NR stereo pass costs, not total application frame time or pure
model-only cost.

Both configurations own 320x256 output pixels per eye. The compact adapter
evaluates its 512x512 bucket, including spare pixels: 3.2 times that owned
area per eye. Source density is unchanged. This compares the final adapter
with its real padding cost, not isolated allocation capacity at identical
evaluated shape. Smaller storage does not imply less native work.

Active logical transport falls from 36,126,720 to 8,388,608 bytes (76.78%).
`nativeAllocationBytes` remains null with
`provider_does_not_expose_residency_bytes`, and
`physicalResidencyMeasured=false`. This is not a net native/driver memory
saving claim. The additional native readable-footprint and temporal-quality
requirements for promotion are not marked passed.

Capture on/off, native stationary images, both camera-motion paths and
AI-on motion completed without NR failure. Compact motion retained its
allocation: resource rebuild count 42 before and after. The 512 result and
the [earlier retained 768 regression](nr-task9-10-ai-off-qualification-20261003.md)
reject production promotion. They do not prove that every future compact
backend or bucket must regress.

## Evidence audit and limitations

Nine bounded windows completed 2,700 submitted/resolved frames, all with
AI off. All 135 available relevant CPU/GPU timer histories were verified
against their exact capture IDs and 300-sample counts, along with the
performance guard and runtime identity receipts. Full stage tables and
raw histories are retained; individual profiler sample source IDs remain
unavailable. The starting small-view bracket also has 8.70% drift and is
not the basis of the stable compact comparison above.

Nine native sequences contain 128 distinct stereo pairs / 256 PNG files
at 1512x1680 per eye. Every pair passed source-frame, both-eye commit,
outer composition, disjoint output, artifact SHA-256, PNG chunk CRC and
decompression/dimension checks. The sequence manifests retain actual
acquisition times; scheduling ordinals are not substituted for source
frames. Still images add 12 artifacts, bringing the screenshot worker's
completed-artifact count to 268, with zero failed artifacts.

Inspection of the starting/closer native stills, the full/compact closer
left images and a compact-motion right image found no obvious rectangular
seam. This limited inspection and the structural audit do not establish
blinded temporal/stereo perceptual equivalence. Movement sequences were
captured outside timing windows.

Two local assertions were too strong: expecting compact to survive a
newly observed unsupported shape, and expecting a long calibration to
survive changed keys. Their failed attempts remain preserved. A free-camera
readback assertion also incorrectly equated native drive angles with
world-Euler observations; image matrices and subsequent stationary
readback were used to verify the actual view. No success was inferred
from that failed assertion.

The image sequence reused `close-compact-start/poll` convenience labels.
Both original operations remain in unique immutable invocation journals;
the performance audit indexes the profiler receipts explicitly. One
preview viewer rejected its PNG; the native eye image was viewed, and
the full sequence PNGs passed independent integrity checks.

## Host-controller correction and adversarial review

The fenced AI-off command completed, but the old host semantic adapter
rejected its response for lacking generic `ok`. The successful toggle was
not replayed. The host correction recognizes exact camera completion and
fenced console exec/read contracts. It requires typed fields, exact
command/state matching, complete markers and counted string lines, while
preserving explicit errors and sampler-loss diagnostics. Acknowledgement
still does not prove the intended game effect or rendered view.

Source and packaged controller suites each pass 450 checks. The capture
interaction fixture passes; all three source/package hashes match. Live
camera enable/drive/release and AI-on restoration passed through the
recorded controller. Feedback `AUTO-20261003-222214157-73BA3112` is resolved
with commit `a2223b5`, fast-forwarded into local automation `dev`.
No DevBench server source changed, so no server PR or server build was
created. No push or installed marketplace rotation ran.

Scope/DRY review reused the maintained controller, semantic/error path,
capture orchestrator, profiler and screenshot APIs. No alternate HTTP
client, graphics resource, renderer mutation or production overhead was
added. Adversarial fixtures cover missing/malformed acknowledgement
fields, queued work, wrong state, explicit service errors, missing fences,
bad line counts/types and empty/single/multiple output lines.

The initial sandboxed camera attempt failed on the host evidence-directory
write before dispatch. The authorized elevated controller then continued;
this was not a renderer rejection. The renderer and universal SE/AE/VR
offline validation from the producer's 228-test build remains applicable;
no C++ change or new DLL build occurred here. Live evidence is VR-only.

## Restoration and clean exit

Final native evaluations/output commits: 408,150. Native failures, stereo
failures, validation failures, reset failures, device removals and
quarantines are all zero. Original NR fingerprint
`4f9d26a4169698e79c873cf379f47728`, colour/profiler settings and the exact
original camera observation were restored. Cost search is disabled with
no loaded profile. Global AI is on; later actor motion is intentional.

Screenshot queues/sequences/jobs and native replay capture were inactive.
The owned state recording stopped without a limit or unrecorded tail;
25,551 pose samples and 25,697 tracking samples were persisted, copied to
the evidence root and hash-verified. It is a long observation trace, not
a replay-qualified recording. Console `qqq` ended the exact Skyrim process;
absence was verified at `2026-10-03T22:53:56.7130065Z`. No forced
termination, MO2/SteamVR shutdown, profile change or save was performed.
