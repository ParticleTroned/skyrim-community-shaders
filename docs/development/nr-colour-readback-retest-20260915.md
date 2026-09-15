# NR four-region repeat and readback synchronization

## Measured build and scope

The Dragonsreach `CSXTest01` repeat ran on the Valve null HMD with FOV
enabled and two visible actors for the retained four-region samples.
The installed source was `d9522416b52856217f63ff19692c092a862a3931`,
with producer Build ID
`ce5209aed5e3a452a7b402b5ec68323db8f138fc0e111fa9d0a8ce0c6e60f258`.
The enabled AIO's physical DLL matched its adjacent manifest: 24,725,504
bytes, SHA-256
`06b618cb7234393250c049cad01944561df9f686d8202e6512fb92acf2fa7c3d`.
DevBench was `1.18.1+pt.1.16.1.pr8-ingame.a2a9c0f`.

Raw requests, responses, captures and analysis remain under
`build/validation/nr-live-20260915/retest-f750aa3f3-20260915T085844Z`.
That directory was named before reading the actual producer identity;
these are **d9522416b measurements**, not measurements of f750aa3f3.
`repeat-results.json` retains phase results and frame-correlated outer
publication evidence. This continues the
[earlier retest](nr-colour-multi-roi-retest-20260915.md).

## Four-region and category results

Retained groups use complete API-v3 batches after 16 advancing warmup
frames, matching revision, generation, insertion and source frame.
Four-region groups contain actual slots `[0, 1, 4, 5]`. Reducing the
character margin from 0.25 to 0.05 allowed separate regions to remain
eligible in both eyes. Save reloads restored actors that left view.

| Phase                             | Fresh groups | Four-region groups | Result                                                |
| --------------------------------- | -----------: | -----------------: | ----------------------------------------------------- |
| Early transport, margin 0.25      |          120 |                  0 | Numeric transport passed; four-region coverage absent |
| Early transport, margin 0.05      |          120 |                 26 | Four-region transport passed                          |
| Early actual NR, Managed identity |           80 |                  8 | Four-region inference evidence passed                 |
| Initial late transport            |          120 |                  0 | Numeric transport passed; four-region coverage absent |
| Late transport after reload       |          160 |                  2 | Partial coverage; supplemented by final repeat        |
| Late actual NR, Managed identity  |          120 |                 70 | Four-region inference evidence passed                 |
| Final late transport              |          160 |                146 | Four-region transport passed                          |

Transport maximum RGB error was zero. Actual inference had coherent
fresh per-region samples and successful stereo publication evidence.
All measured phases retained zero renderer and character-preparation
failures. Source-relative numeric checks do not establish the correct
NVIDIA colour domain or physical headset presentation.

Each insertion completed 40 category changes over 200 scenario steps,
alternating face-only at strength 1 with face, skin and hair at strengths
0.5, 0.75 and 0.5. Across the 80 changes, no prepared source-frame policy
or strength mismatch occurred, and preparation/renderer failures stayed
at zero. The late repeat added no readback fallbacks; the early repeat
added four right-eye fallbacks, increasing its counter from 39 to 43.
The left-eye counter stayed zero. These recovered readback failures are
retained separately from successful category transitions.

At final frame 91949, the right eye had 56 fallbacks in 54,948 attempts,
with last failure frame 91906; the left had zero in 56,467 attempts.
The current right-eye plan had recovered to a split. These cumulative
counts include activity outside the bounded category scenarios.

## Readback correction

Observed failures reported `current_copy_timeout` while waiting for the
right-eye GPU copy. The existing shared deadline already excluded CPU
region planning, so moving that planning alone had not removed failures.
The exact cause of the GPU or driver delay is not established.

The correction queues a D3D11 completion fence after both current-mask
copies. Both staging reads use that signal before either buffer is
mapped. Microsoft's [Signal contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_3/nf-d3d11_3-id3d11devicecontext4-signal)
orders completion after preceding GPU work. A monotonic signal value
distinguishes each batch; device/resource reset releases the owned fence.

The shared 50 ms deadline, nonblocking Map, source identity checks,
conservative projected fallback and retained failure counters remain.
Unavailable fence interfaces use the existing bounded event-query path
for compatibility. Fence creation/signal errors are explicit failures;
they do not silently admit data through an older query. DevBench exposes
`maskRoiReadbackFenceValue` in the eye diagnostics, registered description
and output schema. Zero denotes query compatibility or no pending copy.

The WARP regression delays the right copy after a ready left copy,
checks that the batch cannot be read prematurely, then verifies exact
bounds after completion. It also checks expired-deadline ready reads,
invalid/failed signals and rejection of a previous batch's completed
value. Compilation and execution of these new checks are pending at
this implementation commit, as the user requires committing first.
Disappearance of intermittent live failures needs the rebuilt DLL.

## Remaining evidence limits and cleanup

Exposure now identifies the live replacement shader correctly, but its
AvgTex view is 2x2, format 26, mip 0, one mip/array slice/sample. The
scalar 1x1 guard rejects it rather than guessing spatial exposure.
Successful engine exposure capture remains unverified.

The healthy-backend insertion-switch correction was absent from the
installed DLL, so its recovery duration remains unmeasured. Controlled
performance, physical headset presentation and final colour-domain
selection remain unqualified. No 30-minute recording test was run.

The repeat recording stopped at 972,610 ms, with 7,851 pose samples,
8,461 tracking samples, 52 activity events and zero unrecorded tail.
The saved activity SHA-256 is
`4c47b6ed13f56172281c3642dd12af288bc0674712a02bbec4f9d20e1f42e7f9`.
The final early category scenario ran after recording stopped and is
preserved in the complete direct-MCP transcript. Transient approval
service capacity errors cleared on retry of the authorized actions.
All owned captures and recordings were inactive; the attached game
was left running. No AIO validation campaign was run.
