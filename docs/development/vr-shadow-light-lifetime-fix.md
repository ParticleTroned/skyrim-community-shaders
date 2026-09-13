# VR light ownership during COC

## Observed failure

The 2026-09-12 diagnostic run in SkyrimVR PID 5236 completed its first
Dragonsreach-to-Windhelm transition and its return. The next admission
receipts independently identify `WindhelmExterior01` (cell 46267), then
`WhiterunDragonsreach` (cell 91555), as their source cells. The third COC
froze while leaving Dragonsreach; arrival in Windhelm was never observed.
Only three of the requested 26 commands were dispatched.

The shadow journal recorded 1,033 first-chance access violations on the
main thread in frame 64346. Matching private symbols and Ghidra analysis
of captured process code locate 138 faults at the second `IsShadowLight()`
call in Light Limit Fix's geometry lighting setup. Another 895 attempted
to execute unmapped address `0x33509950`; their original caller cannot be
reconstructed because the journal did not retain RCX or the return address.

Two known shadow lights had been destroyed by worker thread 9748. Their
later heap snapshots contain descriptor pointers where a live light needs
its vtable, and the resulting virtual slots explain the observed invalid
call targets. Generation 28's destruction ended 25.8 microseconds before
the first exception. This strongly supports use after destruction through
the render pass's unowned `sceneLights` entries.

The full dump and subsequent live attachment separately show the main
thread stuck in SteamVR submission, inside NVIDIA's D3D11 `Flush` wait.
The connection between that wait and the invalid-light accesses remains
unproven. This run also does not establish that earlier parabolic
descriptor/accumulator CTDs have the same cause.

## Logging and exception handling

The diagnostic's vectored handler returns `EXCEPTION_CONTINUE_SEARCH`.
It does not skip instructions, suppress exceptions, or change ownership.
The existing Light Limit Fix `__except` handler clears strict-light data;
this explains why these accesses can be handled instead of immediately
terminating the process. Journal overhead can change thread timing, so
the different visible outcome cannot establish that logging prevented a CTD.

## Ownership change

VR geometry setup now takes a frame-scoped snapshot of the active shadow
scene node's owning light lists. It copies `NiPointer` references while
holding `lightQueueLock`, then releases the lock before reading lights or
performing rendering work. The native queue drain at SkyrimVR+`0x12F77E0`
holds this lock at node+`0x1D0` while removing active entries and releasing
queued references. The independently inspected removal path at
`0x12F7E10` uses the same lock. Runtime data accessors supply these fields;
the implementation adds no executable patches or hardcoded addresses.

Both strict-light and shadow-mask loops treat pass pointers only as keys
into this retained set. An unknown pointer is skipped without reading it
or incrementing a potentially freed reference count. Pending add/remove
queues are also retained because existing passes can still refer to their
lights. Only entries captured from active lists enter the clustered-light
enumeration, preserving their order and avoiding duplicate lights.

The clustered-light path shares the snapshot instead of walking mutable
engine lists directly. References are released by the existing frame and
post-load resets, outside the engine lock. Retained addresses cannot be
reused until reset. Lights added after capture become eligible after the
next reset. SE and AE retain their existing light enumeration and geometry
setup; the locking contract was inspected in VR's native code.

Capture and cache publication are transactional. A local snapshot is filled
under the queue lock and published only after unlocking. Allocation failure
releases its references after unlocking, logs the failure, and suppresses
further capture attempts until reset. Strict-light output is cleared, and
clustered-light output is cleared if its capture fails. Previously retained
snapshots stay alive until reset even after a subsequent capture fails.

This change addresses the identified Light Limit Fix consumer. It does not
claim to repair every engine consumer of raw light pointers or the separately
observed presentation hang.

## Adversarial review

### Native shadow render ownership

The later PID 22880 run exposed a separate native consumer: generation 28
was destroyed by a loading thread while its parabolic render call was
active. Destruction ended 0.6308 ms before render returned. The native
function reads the light's descriptor count at SkyrimVR+`0x137140C` after
the descriptor-render call. No exception was recorded in this run. The
zero-length NVIDIA command observed in the hang remains a separate finding;
the capture does not establish which code wrote that command.

The VR native shadow loop now takes its own local owning snapshot. It copies
the active and pending owning lists and the accumulated render order under
`lightQueueLock`, then releases the lock before virtual dispatch. The owning
references survive the entire loop, independently of LLF frame/load resets.
Rendering and final reference releases occur outside the engine lock.
The shared `SceneLightSnapshot::RetainScene` method supplies the same list
coverage to both native rendering and existing LLF consumers. Native capture
omits the unused active-light enumeration and copies render order in one
range assignment to avoid repeated vector growth under the queue lock.
An empty, exhausted or initially null-terminated pass returns under the
same lock before constructing the snapshot. It performs no allocation or
light-reference acquisition.

The hook replaces the raw selection/dispatch loop at SkyrimVR+`0x13231FB`
through `0x1323230`. Ghidra analysis of the retained PID 22880 image verifies
the native selector at `0x12FA250`: it indexes the raw array at node+`0x258`.
The replacement checks keys against retained owners before reading a light
or its virtual table. It preserves the original render order, the native
index passed by reference, and null-terminated traversal, and additionally
bounds traversal by the captured array size. An unknown owner, a non-shadow
object, or a non-advancing index ends the pass. Allocation failure skips the
pass after releasing the lock and any acquired references. Rendering
exceptions propagate normally while local references unwind.

Installation is restricted to Skyrim VR 1.4.15 and requires all 184 bytes of
the native function to match before either write. This covers the setup of
RSI and the stack index, the 53-byte loop, and the continuation's state and
register restores. A mismatch logs the refusal and leaves the function
untouched. The native call enters a register adapter
that tail-jumps to the C++ replacement. This preserves the native return
address and unwind metadata; the replacement returns through a patched
jump to the original loop continuation. Existing native render virtual hooks
remain in the call path. This fix has no dependency on the shadow-lifetime
observer or driver-command recorder. SE and AE receive no new executable
patch or render-path change.

Focused validation after the second PR #96 adversarial review:

-   `pwsh artifacts/pr96-adversarial-review-2-20260913/build.ps1 -Focused`
    passed.
-   `ctest --test-dir build/native-shadow-render-pr-build -C Release -R "^(SceneLightSnapshot|VRSceneGuards)$" --output-on-failure -V`
    passed both tests in 0.15 seconds. The ownership harness exercises concurrent worker
    teardown during rendering, raw-array clearing, pending owners, rejected
    stale keys and non-shadow lights, native index advancement and bounds,
    release of the last reference on render exceptions, nine existing capture
    allocation failures and four native capture allocation failures. It also
    verifies that empty passes allocate nothing, index wraparound stops the
    loop, and later lights survive teardown during the first render call.
    The machine-code harness passes 744 assertions, including every-byte
    function mismatch rejection, runtime scope, installed branch targets,
    argument transfer through the installer-generated adapter, stack
    alignment and native return identity. It executes the captured prologue
    and register restores with the native caller-home-space index location,
    verifies saved-state restoration, and exercises the return jump over
    displaced instructions filled with traps.
-   All 184 fixture bytes match the retained PID 22880 capture, SHA-256
    `4859ce0f79962f3574e830c48d23d75f3798234e87e0aeea5ec825b5db9322e4`.
-   Runtime testing is reserved for the user. This implementation has no
    in-game stability or performance result yet. It protects light lifetime;
    arbitrary concurrent changes to light fields and other engine objects
    are outside this contract.

The review found an incomplete instruction-admission check and unnecessary
capture work on empty passes; both are corrected above. The existing
snapshot helper, byte-check helper and trampoline remain shared. The scope
stays confined to native shadow lifetime, with no diagnostic dependency.
The second review found no additional production-code defect. It corrected
the simplified-frame test gap above; production sources remain identical
to the validated DLL source `90d545008c1dda2b0d9e9d91b82021281698bbcd`.
No DLL rebuild is claimed for this test/documentation-only revision.

The adversarial review evidence is preserved under
`artifacts/pr96-adversarial-review-20260913/` and
`artifacts/pr96-adversarial-review-2-20260913/`. Initial isolated tests and PR
preparation receipts remain under `artifacts/native-shadow-render-pr-20260913/`.
Earlier native analysis,
build/test receipts and handoff metadata remain locally under
`artifacts/vr-native-shadow-lifetime-fix/`. The full originating
hang evidence remains under
`artifacts/shadow-coc20-5s-pid22880-20260912T114320Z/`.

### LLF snapshot review

-   Correctness: retained references come only from owning engine lists under
    their mutation lock. Both pass loops validate raw keys before dereferencing
    them. Skipping a missing strict light compacts output without changing the
    original shadow-light index test; shadow-mask bits still use engine indices.
-   Robustness: the first candidate cached an empty snapshot before filling it.
    Review changed this to publish only a completed capture, added allocation
    failure handling to both consumers, and prevented repeated failed capture
    attempts within a frame. The cache uses the same standard container as the
    regression harness, including its allocation-failure behavior.
-   Ownership: the tests now use CommonLib's actual `NiPointer`, exercise the
    production capture and enumeration functions, and check worker teardown, all five source
    lists, active ordering, unknown keys, cache reuse, reference acquisition
    under lock, and injected allocation failures. Duplicate entries no longer
    construct a temporary owning entry before the map checks for an existing key.
-   Scope and DRY: strict and clustered consumers share one capture helper.
    SE/AE keep the owning derived-to-base conversion during shadow-light
    processing; changing the shared consumer to accept a raw pointer initially
    lost that temporary reference, which review restored explicitly.
    No existing utility provided this light-ownership contract. The change adds
    no shader, D3D resource, driver branch, runtime patch, setting, or diagnostic
    dependency. Frame and post-load reset behavior remain centralized in `Reset`.
-   Limits: ownership pins `BSLight` lifetime; it does not synchronize arbitrary
    concurrent edits to light fields, rooms, or portals. Address reuse before
    capture cannot be identified by a raw pointer alone. These are not claimed
    fixes. No performance improvement is claimed; capture cost is unmeasured.

## Validation

### Final review revision

-   Universal Release DLL linked successfully with SE, AE, VR and DevBench
    enabled using
    `pwsh tools/cmake.ps1 --build build/shadow-lifetime-20260912 --config Release --target CommunityShaders scene_light_snapshot_test vr_scene_guards_test --parallel 4`.
    The DLL target passed; the following test target initially failed because
    the standalone harness lacked a `BSLight` alias. After adding that alias,
    `pwsh tools/cmake.ps1 --build build/shadow-lifetime-20260912 --config Release --target scene_light_snapshot_test vr_scene_guards_test --parallel 4`
    passed. The build retains an existing FidelityFX CMake deprecation warning.
-   `ctest --test-dir build/shadow-lifetime-20260912 -C Release -R "^(SceneLightSnapshot|VRSceneGuards)$" --output-on-failure -V`
    passed both tests: nine injected allocation failures, production VR and
    non-VR enumeration ownership checks, and 364 scene-guard assertions.
-   The linked DLL matches its adjacent manifest: Build ID
    `7162ff8f970f7f20382bb26b5b3b551fbf6a064c0e1a7608a85b4d0d97bf2452`,
    SHA-256 `f171e10ff815a856d3490dae6107d296e89468c891c3d44bd24876e418c10e4e`,
    28,760,576 bytes. This build includes the separate diagnostic working-tree
    changes; it is not a clean build of the LLF-only commit. Its producer
    manifest and final source hashes are preserved with the review evidence.
-   Scoped whitespace, C++ and Markdown checks passed. Gersemi verified the
    new extractor directly. The focused CMake file and HEAD produce exactly
    the same 395 changed formatting lines; no new formatting debt was added.
    The whole-file gersemi hook is skipped for this commit to preserve the
    unrelated baseline formatting. Both diffs and the comparison are saved.
-   The final revision was not deployed or tested in-game. The earlier
    candidate's runtime evidence below does not validate these final changes.
    No SE/AE runtime or performance qualification is claimed.

Final build, test and formatting evidence is preserved locally under
`artifacts/llf-adversarial-review/`.

### Earlier candidate runtime evidence

The following evidence belongs to the candidate before transactional
hardening and the final compatibility correction.

-   Universal Release DLL built with SE, AE, VR and DevBench enabled:
    `pwsh tools/cmake.ps1 --build build/shadow-lifetime-20260912 --config Release --target CommunityShaders scene_light_snapshot_test vr_scene_guards_test --parallel 4`.
-   Four focused tests passed:
    `ctest --test-dir build/shadow-lifetime-20260912 -C Release -R "^(SceneLightSnapshot|VRSceneGuards|ShadowLifetimeJournal|ShadowLifetimeDecoder)$" --output-on-failure`.
-   C++ formatting checks passed. Full-file CMake formatting exposed existing
    unrelated formatting differences; those edits were reverted after checking
    the pre-task CMake bytes against the preserved producer's SHA-256.
-   The user subsequently launched the candidate in PID 60840. Its physical
    enabled DLL, adjacent manifest, private PDB, and captured producer agree
    on Build ID
    `2e98f8f9cf442af4126744504be68631f02621fdaf67516487d9b99f82f9fedd`.
    The 20-COC diagnostic with five-second pre-dispatch waits completed six
    transitions, then froze on the seventh while leaving Dragonsreach.
    The journal retained 251,586 records with zero exceptions, drops, or
    overwritten records. This supports the targeted ownership change but
    does not qualify the candidate against the presentation hang.
-   SE/AE runtime validation has not run. This diagnostic used the existing
    user-launched session and `CSXTest01`; it is not a canonical stability or
    render-scale performance qualification.

Full local evidence, source analysis, logs and the candidate build receipt
are preserved under
`artifacts/shadow-coc26-5s-pid5236-20260912T054615404Z/ghidra-investigation/`.
The original producer was Build ID
`d1b5a347cf75b29973f8e5a8d22477ce3e4a112b057566f329a7c218da4a7e6d`,
source `befd358515eb201aa5a278292b93204f24c44d9e` with recorded local changes.

## Repeated presentation hang

Ghidra analysis of PID 60840's captured driver code and command memory
identifies the presentation hang more precisely. NVIDIA's CPU command
consumer at `nvwgf2umx+0x598370` reads a 16-bit opcode and a 16-bit length
in dwords, dispatches the command, and advances its cursor by four times
that length. Both fields at cursor `0x2A409AE19D8` are zero, while the
queue end is `0x2A409AE7EC0`. The worker repeatedly executes opcode zero
without advancing. It cannot finish the batch and signal semaphore
`0x17CC`, which the main thread awaits during SteamVR's D3D11 `Flush`.

The malformed entry follows 317 valid-length commands in the captured
batch. A later noninvasive live sample still references the same entry;
the main thread's one-second wait retry count increased from 162 to 667.
This establishes a CPU command-consumer loop, not a GPU hardware failure.
The right-eye submission packet retains its texture and device, with
bounds `[0.5, 0, 1, 1]` and publication generation 18.

The earlier PID 5236 dump has identical consumer code and the same
zero-length condition at cursor `0x18882C943F0`, before end
`0x18882C955C8`. Its worker cursor was independently verified with CDB.
Both freezes therefore share this mechanism, although only the earlier
run recorded shadow-light access violations.

The originating write or incorrect command production remains unknown.
Neither dump retains the history needed to attribute it to the game,
Community Shaders, another plugin, or the driver. No queue patch or
additional runtime fix has been applied. A subsequent reproduction needs
producer/write-history evidence before selecting a corrective change.

The tested candidate DLL SHA-256 is
`8114da4c95f53600425f7df71d579b162e8fce4844c74c69a500393976f0bb10`,
size 28,758,016 bytes. Its preserved source identity is
`feb1bee1f5a30a2c5fc0df114b5f9130cfe2cbe6` with local changes recorded
in the producer manifest. Full evidence, all transition receipts, both
parsed driver queues, and the Ghidra project are under
`artifacts/shadow-coc20-5s-pid60840-20260912T094551130Z/`.
The full dump is 27,091,167,008 bytes, SHA-256
`16165ce6cf3640cc380a93eb88f07ea0a46603384ffe7e6cd0b597c8ce14832d`.
At capture time the game was frozen and shadow/stress captures were active.
The saved journal is a coherent snapshot with zero active writers, not a
flush-confirmed finalization. A subsequent process inspection found the
game and MO2 closed; no later journal finalization is claimed.
