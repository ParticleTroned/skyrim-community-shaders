# VR submit input contracts

The submit-stage path captures stereo camera constants before the engine's
post-processing chain and carries an explicit description of submitted color
to DLSS and FSR. These boundaries preserve the existing render-scale
controller, resource retirement, menu transactions, and compositor ownership.

## Temporal capture

`Main_PostProcessing` captures once after preparing and latching the current
frame's history-reset state. The producer must be the current world frame
with an active submit-stage vendor path. The captured tuple contains:

-   Engine frame, compositor cycle, vendor method, resource-contract generation,
    and per-eye render/output dimensions.
-   Both eyes' inverse view, unjittered projection, current/previous unjittered
    view-projection matrices, and current/previous camera-position adjustments.
-   Jitter, near/far planes, vertical camera FOV, frame time, and the reset
    state known at capture.
-   Strong references identifying the producer's depth and motion textures.

A repeated visit with the same producer and contract retains the first capture.
A changed contract in that producer cycle, invalid input, or resource reset
prevents reuse until a subsequent producer. The submit boundary requires the
exact compositor cycle, generation, method, dimensions, and source resources
before selecting temporal reconstruction. Missing evidence uses the existing
spatial presentation path and requests a temporal-history reset.

Desktop Present may advance the engine frame counter without starting another
compositor cycle. That does not by itself discard the camera snapshot. Before
the first observed compositor boundary, matching remains strictly frame-based.
A new compositor cycle also requires a new logical producer frame. Repeating
the same frame in a later cycle invalidates capture and uses spatial
presentation until a subsequent producer. This prevents another temporal
history update with the same jitter sample and Streamline frame token.

Submit preparation, vendor output, FSR stereo batches, foveated center work,
periphery pairing, and mirror pairing include this producer identity.
Input freshness admission remains authoritative: reusing peer inputs also
requires the exact outer pair proof, current completed world frame, and
retained color/depth/motion resources. Advancing Present alone does not prove
that GPU inputs are still reusable. A new cycle cannot reuse old output just
because the engine frame counter stayed unchanged.
Invalidated cache entries remain invalid even when their cycle still matches.
The FSR stereo batch retains its successful dispatch evidence with the output.
An admitted reuse preserves the original dispatch frame, serial, and backend
in both presentation evidence and fidelity observations.

The dispatch scope owns a local copy and restores its previous state on every
exit. DLSS, FSR, and foveated periphery processing consume the captured camera
and jitter within that scope. Ordinary main-pass rendering retains its own
inputs. Snapshot storage and dispatch binding belong privately to Upscaling;
a shared accessor selects dispatch jitter. Existing Streamline frame-token
coordination and crop transforms remain authoritative.

DLSS still requests the captured logical frame's Streamline token. If a newer
token has already been published, the existing coordinator rejects the older
request and presentation falls back safely. The integration never rewinds the
coordinator or assigns old camera metadata to a newer token.
Dispatch-failure cleanup preserves the published token while clearing cached
constants. The cycle is locked to spatial fallback before attempting that
fallback, so a failed stretch cannot reopen vendor admission for the peer eye.
Lifecycle resets retain their existing ability to clear token publication.
Deferred FSR presentation restores the cycle's color contract when resource
replacement has cleared admission, and rejects a conflicting contract.

History reset is monotonic: a reset requested after capture remains effective
for same-frame fallback and subsequent dispatches. Capturing a false reset
value cannot cancel a later lifecycle or vendor-failure reset.

Auxiliary GPU encoding keeps its existing shared shader constants and
submit-stage location. The snapshot freezes camera metadata and retains source
identity; it does not
claim that a retained texture's texels are immutable. Moving or duplicating
GPU capture requires separate state-restoration and performance validation.

## Color contract

The supported presentation source remains `R8G8B8A8_UNORM`. Storage format,
transfer function, source dynamic range, and vendor processing mode are
separate concepts.

| OpenVR color space            | Resolved source | Presentation                         | Temporal vendor path                    |
| ----------------------------- | --------------- | ------------------------------------ | --------------------------------------- |
| Auto                          | Gamma, LDR      | Existing path                        | Existing path                           |
| Gamma                         | Gamma, LDR      | Existing path                        | Existing path                           |
| Linear                        | Linear, LDR     | Spatial, original metadata preserved | Withheld pending a separate integration |
| Unknown or unsupported format | Unsupported     | Original fallback rules              | Withheld                                |

Auto and Gamma resolve to the same contract for the supported 8-bit source.
Color admission is fixed for a compositor cycle and included in output-cache
identity. A peer eye cannot silently switch transfer function, and a changed
color contract cannot reuse an old vendor output. Color-related vendor
fallback requests a history reset before temporal reconstruction resumes.

DLSS receives an explicit LDR processing selection from source meaning,
independent of texture storage. The established FSR processing flags remain
unchanged and are named as a legacy processing policy; those flags do not
relabel the captured source as HDR. This change introduces no gamma conversion,
new floating-point color buffers, or claim of improved color fidelity.

The selective design lessons came from
[Open Shaders PR #625](https://github.com/alandtse/open-shaders/pull/625),
reviewed at `bb776e8bb2b36237e7f22141d653f27e17525927`. Its complete
submit/menu implementation is not imported.

## Validation

`VRSubmitTemporalSnapshot` exercises immutable capture, both-eye payloads,
frame/generation/method/dimension mismatch, invalid numeric values,
same-producer invalidation, repeated logical frames across cycles, frame/cycle
ordering, and late history resets. `VRSubmitStereoBatch` exercises the actual
batch cache's compatibility and retained dispatch evidence across Present,
including frame-zero normalization without changing raw cache identity.
`FSREyeDispatch` compiles the production dispatch and deferred presentation
paths, checks Linear rejection and captured host scalars, and preserves
color admission through deferred recovery.
`StreamlineFrameTokenPublication` checks that failure cleanup cannot reopen
an older token, while lifecycle reset still permits a fresh publication.
`VRSubmitColorContract` covers Auto/Gamma equivalence, Linear spatial
admission, invalid contracts, and
separation of source range from vendor processing mode.

Run the controller tests with the normal CMake wrapper:

```powershell
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target vr_submit_temporal_snapshot_test vr_submit_color_contract_test vr_submit_stereo_batch_test streamline_frame_token_publication_test
ctest --test-dir build/ALL -C Release -R '^(VRSubmit(TemporalSnapshot|ColorContract|StereoBatch)|StreamlineFrameTokenPublication)$' --output-on-failure
```

Runtime acceptance still requires the generated report from
[render-scale PR qualification](render-scale-pr-qualification.md) using the
candidate DLL and a matching accepted fixture/baseline. Additional focused
checks should cover camera/menu changes between capture and submission,
foveated fallback after a late reset, and Gamma/Linear/Gamma submissions.
Policy tests and a successful build do not establish runtime visual quality
or performance. No measurement belongs in the comparison ledger until that
runtime evidence exists.
