# CSX Editor API v1

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

## Mutations

Supported actions are `open`, `close`, `toggle`, `reset_layout`, and
`exit_preview`. Every mutation requires:

1. a fresh snapshot and its `stateRevision`;
2. preflight with the complete mutation;
3. execution with identical fields and the returned token within 30 seconds.

Closing or toggling while preview mode is active, and explicitly exiting
preview mode, require `allowDisruptive=true`. The service does not expose
enter-preview, record editing, saving, undo, time control, or weather control;
those have broader side effects and remain with their owning UI/domain APIs.
