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
before selecting temporal reconstruction. Missing evidence uses the existing spatial
presentation path and requests a temporal-history reset.

Desktop Present may advance the engine frame counter without starting another
compositor cycle. That does not by itself discard the camera snapshot. Before
the first observed compositor boundary, matching remains strictly frame-based.

Submit preparation, vendor output, FSR stereo batches, foveated center work,
periphery pairing, and mirror pairing share this producer identity. A second
eye after desktop Present reuses the cycle's work; a new cycle cannot reuse
an old output just because the engine frame counter stayed unchanged.
Invalidated cache entries remain invalid even when their cycle still matches.

The dispatch scope owns a local copy and restores its previous state on every
exit. DLSS, FSR, and foveated periphery processing consume the captured camera
and jitter within that scope. Ordinary main-pass rendering retains its own
inputs. Existing Streamline frame-token coordination and crop transforms
remain authoritative.

DLSS still requests the captured logical frame's Streamline token. If a newer
token has already been published, the existing coordinator rejects the older
request and presentation falls back safely. The integration never rewinds the
coordinator or assigns old camera metadata to a newer token.

History reset is monotonic: a reset requested after capture remains effective
for same-frame fallback and subsequent dispatches. Capturing a false reset
value cannot cancel a later lifecycle or vendor-failure reset.

Auxiliary GPU encoding keeps its existing shared shader constants and
submit-stage location. The
snapshot freezes camera metadata and retains source identity; it does not
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
same-producer invalidation, frame/cycle ordering, and late history resets.
`VRSubmitColorContract` covers
Auto/Gamma equivalence, Linear spatial admission, invalid contracts, and
separation of source range from vendor processing mode.

Run the controller tests with the normal CMake wrapper:

```powershell
pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target vr_submit_temporal_snapshot_test vr_submit_color_contract_test
ctest --test-dir build/ALL -C Release -R '^VRSubmit(TemporalSnapshot|ColorContract)$' --output-on-failure
```

Runtime acceptance still requires the generated report from
[render-scale PR qualification](render-scale-pr-qualification.md) using the
candidate DLL and a matching accepted fixture/baseline. Additional focused
checks should cover camera/menu changes between capture and submission,
foveated fallback after a late reset, and Gamma/Linear/Gamma submissions.
Policy tests and a successful build do not establish runtime visual quality
or performance. No measurement belongs in the comparison ledger until that
runtime evidence exists.
