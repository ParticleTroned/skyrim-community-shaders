# CSX Editor API v1.1

`csx.editor` is a versioned, process-local inspection and bounded-control API
for the CS Editor. It is additive: the existing launcher button, key binding,
menu session, and direct C++ entry points remain unchanged.

## Discovery and affinity

Native clients discover `CSX::EditorAPI::Interface001` through
`CSserviceapi.h`. Registry metadata is safe to inspect off-thread. Snapshot,
preflight, and execution calls are main-thread-affine and return `kWrongThread`
otherwise. DevBench exposes the same contract as
`communityshaders.editor_api` and dispatches live calls to the game thread.

The service remains registered for the process lifetime. `available` and
`unavailableReason` report lifecycle readiness; absence is not represented by
unregistering the service.

## State contract

The snapshot reports editor/data/resource readiness, whether opening is
currently safe, editor and overall CSX menu-session state, main/loading menu
gates, save/load and persistence guards, preview mode, weather lock, paused
time, undo availability, state revision, capability mask, and producer build
identity.

`Snapshot001` remains byte-for-byte unchanged in v1.1. Native clients detect
Light Editor picker control through `kCapabilityLightPickerControl`; no fields
were appended because existing clients allocate the exact v1.0 snapshot size.
The native registry continues to publish the original 1.0 interface alongside
1.1. A client whose query caps `maximumMinor` at 0 therefore keeps resolving
the 1.0 schema and capability mask, while clients accepting minor 1 receive the
picker capability. The 1.0 interface rejects the three picker-control mutation
values even if a caller constructs their numeric values directly.

The DevBench JSON snapshot additionally reports a `lightEditor` object with
`selected`, `enabled`, `viewportVisible`, `pickerActive`, and
`deferredWorkPending` fields. The last field remains observable after the
editor closes so automation can wait for temporary reference disable/respawn
work to finish.

## Mutations

Supported actions are `open`, `close`, `toggle`, `reset_layout`,
`exit_preview`, `open_light_editor`, `begin_light_pick`, and
`cancel_light_pick`. Every mutation requires:

1. a fresh snapshot and its `stateRevision`;
2. preflight with the complete mutation;
3. execution with identical fields and the returned token within 30 seconds.

Closing or toggling while preview mode is active, and explicitly exiting
preview mode, require `allowDisruptive=true`. Opening Light Editor and starting
its picker also require that acknowledgement because they enable live editing
or capture editor pointer input.

`open_light_editor` opens CS Editor, selects the Light Editor category, and
enables it. Automation must then obtain a fresh snapshot and wait for
`lightEditor.viewportVisible=true`; this means the most recent editor UI pass
rendered a finite, positive-size preview image in a non-collapsed Viewport
window.
`begin_light_pick` requires that state, an available game world, no preview
mode, no pending reference cleanup, and no save/load mutation guard.
`cancel_light_pick` only cancels an active pick; it does not close or disable
the editor. Picker input continues to use the normal ImGui frame and VR
ownership path rather than a separate controller poll.

The service does not expose enter-preview, record editing, saving, undo, time
control, weather control, or a synthetic pick result. Those have broader side
effects and remain with their owning UI/domain APIs.
