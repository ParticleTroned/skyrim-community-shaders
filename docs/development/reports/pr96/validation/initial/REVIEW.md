> Historical report, privacy-filtered for this export. Statements about
> current process state and planned work describe the original session.
> See the bundle README for later findings and omitted source files.

# Native shadow-render lifetime review

Scope: seven production, test and documentation files, extracted from the
investigation workspace onto main-VR after PR #93. The driver recorder,
shadow journal, exception hooks and diagnostic DevBench/CMake changes are
excluded.

The retained native disassembly confirms that the caller initializes its
index at stack +0x60, stores the scene node in RSI, and performs virtual
Render calls through slot 0x50. The adapter transfers those existing
arguments and tail-jumps to C++, retaining a return address inside the
native caller. All 53 replaced bytes are checked before either patch write.

Scene references come only from the owning lists under lightQueueLock.
The accumulated raw array supplies lookup keys and ordering, never new
references. The local snapshot survives LLF resets and native rendering;
virtual dispatch, exceptional unwinding and final release occur after the
lock is released. Invalid keys, non-shadow objects and non-advancing
indices end the pass; allocation failure skips it without publishing a
partial capture.

Preparation refinements:

-   Reuse RetainScene for common ownership-list coverage.
-   Suppress unused active enumeration in the native snapshot.
-   Copy accumulated order in one range assignment.
-   Exercise rejection of a retained non-shadow light in the production
    function harness.

Passed: SceneLightSnapshot and VRSceneGuards, 0.12 s combined, including
nine existing and four native allocation-failure cases, concurrent
teardown during rendering and 477 machine-code/installation assertions.
The scoped commit hooks passed. Original source files remain in the
investigation workspace and were not staged there.

This review does not prove the earlier driver hang's full causal chain.
The native fix has not been tested in-game or measured for performance.
Its snapshot allocations and reference-count operations have a cost; no
performance-neutrality claim is made. The 70 COCs of PR #93 used a DLL
without this native-loop correction and do not validate it.
