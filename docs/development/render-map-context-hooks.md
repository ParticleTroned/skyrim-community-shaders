# Context hook CPU qualification

`RenderMapContextHooks` compiles the actual `D3DContextHooks.cpp`, `Runtime.cpp`
and `Collector.cpp` with a small test driver. Its target-private
`CSX_RENDER_MAP_CONTEXT_TESTING` binding supplies exact function pointers for
controlled originals and excludes hook installation. Ordinary builds retain the
`REL::Relocation` original storage and installer. The thunk and observation
helper bodies are shared; the test does not reproduce their decisions.

The fixture implements the actual SDK COM interfaces. Unexpected methods fail;
selected getters provide caller-owned CPU objects and balanced transient
`AddRef`/`Release` calls. No D3D device, GPU work, installed detour, game or external
process participates. Type-level Windows SDK use is not a live driver test.

Cases exercise draw/dispatch, all six SRV setters and queried state, resource
description, Map HRESULT/output fields, FinishCommandList output and failure,
PS/VS/CS shader shadows and restore-false ExecuteCommandList. Originals and
getters can stop capture, start a successor or start an initially inactive
capture. Tests check original call count, arguments, results, COM references,
stale observation rejection and successor command recording. Native exceptions
must propagate without invented post-call observations.

The existing `RenderMapRuntime` target remains supplementary coverage for runtime
policies and the renamed publication facade. Its helper cases do not replace
concrete-thunk qualification. Neither host fixture qualifies ordinary installer
execution or real rendering; native-source and live capture evidence remain
separate. Compile both non-default targets through their owning strict recipe,
then retain actual assertion and resource-cleanup results.
