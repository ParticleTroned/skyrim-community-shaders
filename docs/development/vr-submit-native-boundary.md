# Native VR submit boundary

The Skyrim VR `BSOpenVR` vtable slot 3 accepts a raw DirectX texture:
`void(BSOpenVR*, ID3D11Texture2D*)`. It constructs an OpenVR
`Texture_t` on its stack, sets DirectX/Gamma and default submit flags,
and submits the two horizontal halves. It does not accept OpenVR bounds
or flags from its caller. The CommonLib declaration currently describes
the compositor interface instead of this native adapter.

The hook must preserve the raw texture argument and model the descriptor
that the engine constructs. Interpreting the texture object as a descriptor
reads its COM object storage as metadata; reading nonexistent flag
arguments compares unrelated register contents with `Submit_Default`.
Both mistakes invalidate otherwise eligible producer boundaries.

The corrected hook constructs an expected descriptor locally and captures
its resource identity for the duration of the native call. Nested submits
still require the matching resource, flags, thread, frame and compositor
cycle. Completed world/guide frames, generation, dimensions and retained
resources remain required before peer inputs can be consumed. Null and
nested native calls cannot establish an independent stereo owner. The
existing lock and post-return relatch sequencing are unchanged.

A separate thread-local call guard covers the entire hook, including
suppressed calls and post-return relatch service. Boundary absence alone
cannot identify an outermost invocation: a nested call clears its boundary,
and a null outer texture never establishes one. Neither case may let a
deeper call admit peer inputs or independently service a relatch. The guard
and previous boundary/completion state restore together on every exit.

## Investigation evidence

The incorrect hook was introduced by `a4cb755c3` (PR65) and remains in
`b46f8f34f916abcba481f6d92c5d0fbf3dabc16a` and `5614c4518`.
The freshness policy and boundary/reuse helpers are unchanged between
PR65 and the tested `b46f8f34` head.

Preserved PR65 run `gameft-sw-20260915T201611526Z` records zero accepted
outer boundaries and zero DLSS prepared-input/output reuse hits. Its
save-13 +59-second snapshot contains 303,456 `FlagsMismatch` and 646
`ResourceMismatch` outcomes. The mismatch also appears in later runs.
Those are cumulative counters, not per-frame timing measurements.

Without the pair proof, guide preparation occurs separately for each
eye. The first eye's vendor work precedes the second preparation, rather
than preparing both eyes before vendor evaluation. This is a concrete
change in command ordering and CPU setup work. Existing counters show
approximately two vendor eye attempts per engine frame; they do not
establish duplicate DLSS evaluation or quantify GPU synchronization cost.

The adapter ABI was checked against 464 bytes of loaded Skyrim VR code
starting at RVA `0xC53920` on September 15, 2026. Their SHA-256 is
`82d147802fbdc8b2815e4315d6d28812504fa83ec928d849850ab9e0663d3913`.
At offsets `+0x0D` and `+0x50`, RDX is saved directly as the nested
descriptor's handle. Offsets `+0x57` and `+0x5B` store DirectX/Gamma;
`+0x8D` stores default flags for the compositor call at `+0xB0`.
The adapter never consumes incoming R8/R9 as bounds/flags. The on-disk
Steam executable has packed code and was not used as disassembly proof.

This identifies an inherited correctness defect and a performance
candidate, not a measured explanation of the complete CPU/GPU regression.
The earlier interval also includes wetterness, shader-cache and capture
changes. The existing game-ft and gameft-sw runs remain separate evidence
because their tracing and save sets differ.

## Validation

`VRRelatchNativeBoundary` compiles the production native hook and eye
completion code against a callback with the native raw-texture signature.
It uses the real freshness boundary helper and OpenVR descriptor, rather
than a mocked resource extractor. It checks raw argument forwarding and
producer identity for both eyes alongside complete, incomplete, duplicate,
invalid, three-level nested, null, changed-frame/thread/cycle and
throwing-call cases. Nested calls occur between the outer eye completions
to check preservation of an already partially completed pair. Typed injected
failures cannot hide unrelated assertion failures.

`VRSubmitInputFreshnessComposition` and `VRSubmitInputFreshnessPolicy`
retain the negative checks for resource, flags, world/guide frame and
scope changes. `VRSubmitInputFreshnessContract` checks that peer reads
remain behind producer admission.

The native-signature regression rejected the old hook at compilation. The
adversarial three-level nesting test then failed against the initial ABI
correction and passed after the independent call guard was added. The five
focused CTest cases passed in Release. The complete `InSceneOverlay.cpp`
translation unit also compiled with the production ALL Release definitions,
real CommonLib/D3D/OpenVR headers and warnings treated as errors. This was
an isolated compile check using the existing dependency tree, not a linked
DLL build or runtime test.

Runtime acceptance requires corrected-build boundary outcomes, matching
render dimensions/settings, stereo freshness and visual checks, and
matched CPU/GPU measurements. Unchanged PR65 aggregate reruns cannot
measure this correction. GPU pass timing or queue evidence is needed to
attribute GPU recovery; CPU sampling alone cannot establish it.
