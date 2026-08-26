# Output-merger target observation slice

## Scope

This slice records the render-target and depth-stencil views bound to Skyrim's
immediate D3D11 context and joins the resulting binding-set identity to later
draws. It provides observed output-merger state; it does not assign semantic
roles such as left eye, right eye, scene colour, or scene depth.

The observed calls are:

- `ID3D11DeviceContext::OMSetRenderTargets` (vtable slot 33);
- `ID3D11DeviceContext::OMSetRenderTargetsAndUnorderedAccessViews` (slot 34).

The original call executes first. The observer then records the successfully
submitted arguments while—and only while—an explicitly bounded capture is
active and the call belongs to the known immediate context.

## Identities and order

A target view is keyed within one capture by its D3D object pointer and kind
(`render-target` or `depth-target`). Its serialized ID includes the capture
generation and a capture-local pointer generation. A binding-set identity is
the exact ordered tuple of up to eight render-target observation IDs, the
declared render-target count, and the depth-target observation ID. Null slots
are preserved.

Every observed output-merger call advances the same command sequence used by
stage binds, draws, and dispatches. Repeated binding calls therefore remain
visible in order even when they reuse a binding-set identity. Calls using
`D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL` advance order but preserve the
existing binding.

The collector has independent `maxTargetViewObservations` and
`maxTargetBindingObservations` bounds. Overflow is counted, marks the capture
structurally incomplete, and produces a null join rather than an invented one.

## Deliberate limits

- Capture start clears observed target state. It does not query state inherited
  before capture, so a draw may have no target-binding identity until the first
  in-capture bind.
- A pointer generation records observed semantic reuse; view creation, release,
  resource identity, format, dimensions, sample count, and subresource remain
  unobserved.
- UAVs passed by the combined call are not catalogued in this slice.
- Deferred contexts and command-list execution remain unsupported.
- A target's eye or renderer role is unknown unless a later slice proves it
  through an owning engine boundary or other explicit evidence.

## Live validation gate

Deployment is not part of source validation. Before calling this slice
live-validated, one bounded game run must establish all of the following:

1. The DevBench registry advertises typed output-merger target observations and
   the configured target catalogue bounds.
2. Every non-null draw binding reference resolves to one binding declaration,
   and every target in that declaration resolves to one target-view declaration.
3. Declaration and bind events precede the draws that reference them, with a
   monotonic immediate-context command sequence.
4. Repeated binds reuse identities, explicit unbinds produce a distinct empty
   set, and keep-target calls do not change the active identity.
5. The capture reports no dropped target-view or binding observations for the
   selected bound.
6. Any apparent eye association remains labelled unknown until independently
   established; alternating pointers alone are insufficient.
