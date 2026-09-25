# VR grass culling lifetime investigation and fix

The captured failure is concurrent grass instance-group removal and culling.
The fix protects the native grass node before its traversal borrows a child,
and protects each child's group array through culling and the visibility
write. Offline regression checks pass. In-game stability and matched frame
time qualification remain outstanding; this is not a performance-neutrality
claim or proof of which earlier change exposed the race.

## Fault and owner evidence

The September 25 capture faults at **20:52:32.151160 CEST**, in
`SkyrimVR+0xD8FF3C`, while the culling thread calls a freed group's virtual
frustum function. The run reached 18 measured destinations; transition 19,
Windhelm to Dragonsreach, is inferred because its dispatch receipt was not
retained. Two full dumps contain the same exception, not two independent
reproductions.

-   Culling thread: 22932. Removal thread: 4404.
-   Shape: `0x269DD175000`. Group index: 6. Group: `0x266C512CDC0`.
-   The trace records load, concurrent free, then the stale vtable load.
-   The resulting indirect target is `0x435600003F800000`.
-   The shape still has 20 references in the dump. Retaining the shape alone
    does not protect an individually removed group.

The saved dynamic guard trampoline has no unwind entry. Following its verified
continuation and native unwind records recovers the culling stack:

```text
BSMultiStreamInstanceTriShape::OnVisible  +D8FDA0
BSCullingProcess::Process1               +D99B60
NiNode::OnVisible                       +C9E0D0
BSCullingProcess::Process1               +D99B60
native object-array culling              +D9A450
native culling job                      +1315B40
worker                                  +1322390
```

These names are layout/call-graph identifications, not native debug symbols.
The node in the recovered stack, `0x2620EC78100`, exactly equals
`BGSGrassManager::grassNode` in the dump. Its persistent manager ownership is
the boundary before traversal borrows shapes from its children.

All Ghidra work uses the existing **captured live-memory image**, SHA-256
`1A6FBB7E726491929EA6EAA1DBFD866C48404EDAEFD63BD712F41FB13FFA2CE8`.
No executable-on-disk import was used. Both new detour entry prefixes and the
group-lock address reference match the current full dump.

The earlier 11:14:04.350 CEST capture faults in the same native group-culling
path. The full capture supplies the previously missing owner stack and
fault-time trace correlation.

## Audit since csx3.19.1

The local and GitHub tag both peel to
`18f0c8f726a151ddf7a527ac62a11cf0fe8b4601`. The crashing DLL is compiled from
`058c0ac312090029ff8ff6ab085771d925fdfff2`, Build ID
`a4a0d54243f248eae4749adcc6429309809eced0c6e0461f3e72841cb3918780`.
There are exactly four intervening commits:

| Commit      | Change                             | Relevance                                                                                                                             |
| ----------- | ---------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| `a43433d2a` | Unified preset appearance defaults | No grass density, group ownership or manager implementation change; settings can affect workload timing.                              |
| `41aa14c19` | CPU draw fast paths                | Changes shadow-batch pruning/retirement, renderer lookup and material admission. Native owners/readers remain, but timing can change. |
| `df9f377a5` | Shadow admission work              | Packs full draw identity and combines refresh/admit; preserves retained owners and deferred reclamation.                              |
| `058c0ac31` | Closed map-menu layers             | VR presentation/menu lifetime change, not grass-manager mutation.                                                                     |

No grass implementation or CommonLib submodule change appears in that range.
The source review establishes no direct new grass-manager defect. It cannot
exclude an indirect trigger in the two render refactors or another workload
change. A controlled tag/build comparison is required for causal attribution.

The pre-fix current `main-VR` head, `0f7c8aa71`, has 14 commits after the tag.
Its additional shader, water, upscaling and culling-pool changes were absent
from the crashing DLL. They cannot explain this particular capture. The
full current range was also searched for native grass/group mutation changes;
none was found. This fix is developed on that current head, so testing its
performance requires a baseline built from the same head.

## Implementation and lock proof

`VRGrassLifetimeFix` installs two checked detours for Skyrim VR 1.4.15:

1. `NiNode::OnVisible`, `+C9E0D0`: unrelated nodes immediately forward.
   For the current manager's grass node, acquire `grassShapeLock` at
   manager `+40`, then the native group lock at `SkyrimVR+317C990`, before
   entering the original traversal. Release both after it returns.
2. Native `NiNode` bulk child clearing, `+C9D290`: clearing that same grass
   node acquires `grassShapeLock` around the original operation. Other nodes
   immediately forward.

The existing native locks supply exclusion against native writers. No new
ownership registry, pointer-validity heuristic, shape-reference resurrection,
per-group allocation, trace emission or replacement culling algorithm is
introduced. The native 0/1 lock representation is unchanged.

The live-image audit establishes the following ordering:

| Native path                     | Relevant protection                                                                                                  |
| ------------------------------- | -------------------------------------------------------------------------------------------------------------------- |
| Manager publication `+1C68A0`   | Holds the child lock during attachment and queued group publication; the latter acquires the group lock.             |
| Manager cell removal `+1C6480`  | Removes groups under the group lock, releases it, then acquires the child lock to detach empty shapes.               |
| Direct/queued group publication | Group lock excludes array replacement and slot publication.                                                          |
| Manager bulk clear `+1C6800`    | Previously cleared children without the child lock. The clearing detour closes this path before final child release. |

The culling guard takes child then group, matching publication. It never
acquires the manager's separate map lock at `+38`; that lock initially uses
an upgradeable reservation during removal and is insufficient by itself.
Bulk clearing releases the added child lock before the manager acquires its
map lock. Child ownership remains intact through every borrowed shape call;
group storage remains intact through the virtual call and visibility write.

The downstream append path was also checked for a wait cycle. Native
`AppendVirtual` (`+D99F80`) either appends an owning reference directly
(`+D99F60`) or takes a free record with the nonblocking pop (`+D9B240`) and
enqueues it. The drain (`+D9A280`) appends the record and returns it to the
free pool without acquiring either grass lock. No grass-lock dependency was
found in that consumer. Queue saturation remains a separate native concern;
the existing culling-pool guard is unchanged by this fix.

The protection is specific to the validated native grass-node paths. It is
not a general repair for arbitrary stale objects elsewhere in the scene
graph or for unverified third-party changes to the native locking protocol.

## Adversarial review

| Area        | Review result                                                                                                                                                                                                                                                                 |
| ----------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Scope       | Two grass-specific wrappers; no grass settings, shaders, tracer behavior, shadow ownership policy or refactor reverts. Universal DLL builds; installation is VR-only.                                                                                                         |
| Correctness | Lock begins before the child-array read, not after an unowned shape pointer is loaded. It covers group removal, array growth, visibility writes and last-reference release through bulk clearing.                                                                             |
| Robustness  | Exact runtime and entry bytes are required. Unknown patches are retained without installing these hooks. Both detours commit atomically through the existing checked transaction helper; transaction failure stops initialization.                                            |
| Lock order  | Publication uses child then group. Removal releases group before requesting child. The new reader does not request the manager map lock. Nested clearing of a different child node forwards without reacquiring the grass lock.                                               |
| DRY         | Reuses native ownership/locks, standard RAII, the EngineFix registry and the existing detour transaction helper. No parallel shadow-batch lifetime framework. CommonLib's four-byte native lock type exposes no acquisition methods, so the small local adapter is necessary. |
| Performance | Constant work per node/traversal, no per-group instrumentation and no allocations in either wrapper. Same-node grass traversals serialize because native culling also writes visibility. Contention must be measured in game.                                                 |

The review strengthened the tests to retain the actual borrowed array pointer,
exercise manager/node replacement without cached pointer identity, and check
both detour attachment failure positions. It also caught unrelated whole-file
CMake formatting; only the new test registration remains changed.
The downstream queue audit found no additional lock cycle in the validated
native path; this does not establish compatibility with arbitrary replacement
callbacks from other plugins.

## Validation

Passed:

-   Universal SE/AE/VR Release DLL build, DevBench enabled and Tracy disabled:
    `pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target CommunityShaders --parallel 12`.
-   Production-hook harness tests: group removal, array growth, concurrent bulk
    destruction, removal before borrowing, a replacement object, unrelated-node
    forwarding, nested child clearing, C++ exception unwinding, lock order,
    absent/replaced manager node, runtime/signature rejection and atomic attach
    failures.
-   `ctest --test-dir build/ALL -C Release -R '^(VRGrassLifetime|ShadowBatchHooks|ShadowBatchSubmissions|EngineOverflowGuards|VirtualFunctionHook)$' --output-on-failure`:
    5/5 passed. The existing virtual-hook suite also covers transaction begin,
    thread enlistment, abort and commit failure handling.
-   MSVC AddressSanitizer build/run of the same production-hook harness passed.
-   DLL SHA-256 and size matched its adjacent generated build manifest.
-   Scoped pre-commit checks passed. Gersemi checked the isolated new CMake
    registration and extractor; it warns that the isolated registration uses
    the repository's custom `add_controller_test` command. Whole-file CMake
    formatting was skipped to preserve unrelated existing formatting.

Final reviewed DLL: 29,060,096 bytes, SHA-256
`e9c6bd2c79a7f9cc4d588d4d44951d10ff125d48c06d43df935d55a5a7549000`.
Producer Build ID:
`ec5e4ece577ce9b2146007f7e0f22c34bb8d35e472c780551e2be796e894595e`.
Compiled source is `0f7c8aa71f4a7237698209e7e58e70a613091858` plus the
uncommitted changes recorded by dirty digest
`27995ab44b8dd21b443bb70b8a1e126e12c6a9bb8bf47b10c2b379365c631436`.
The complete producer manifest is retained in the local evidence directory.
This candidate has not been deployed or exercised in game.

The subsequent testing AIO rebuild retains DevBench enabled and Tracy disabled.
`pwsh ./tools/cmake.ps1 --build build/ALL --config Release --target Package-AIO-Manual --parallel 12`
passed. Its producer Build ID is
`f58a05eafbacf38ab8613a53e5e41804811add448dbb00d35f8c43410b23a47e`,
compiled from the same base commit plus dirty digest
`f14a4cb3b662896eb541f0e777a1f0fd2e6783a018fcf15257fd65945f5f8dd7`.
The packaged DLL remains 29,060,096 bytes, with SHA-256
`3fea034a66be7694331ace441894f0c6fc38da67eccf54df1bd70936677f6124`.
The archive `CSX_AIO-VR-GrassLifetime-DevBench-20260925-f58a05eafbac.7z`
contains 362 files and is 91,242,317 bytes, with SHA-256
`6782a13bd7ad58915cb793a38714a4478fd3c239c1fc1f6c13b9fe491e012905`.
Archive integrity, complete staging inventory, extracted DLL/manifest/symbol
identity and canonical Build ID checks passed. Receipts and source snapshots
remain under `aio-package-20260925T201258Z/` in the local evidence directory.
Shader tests were not run; the existing build configuration disables them and
this change modifies no shaders. In-game qualification remains outstanding.

Six rotating-order Release overhead rounds, one million calls per lane:

| Mock callback lane                 | Median ns/call | Minimum | Maximum |
| ---------------------------------- | -------------: | ------: | ------: |
| Native callback baseline           |         6.7016 |  6.4787 | 15.1223 |
| Unrelated node through hook        |         8.4540 |  7.8380 |  8.8268 |
| Grass node, both locks uncontended |        39.0806 | 31.4850 | 57.5363 |

This measures wrapper overhead only. Native culling, contention, frame-time
tails and GPU time are absent; these numbers do not establish game performance
neutrality. The required live checks are the traced 25-transition reproduction
and a matched warmed comparison with identical scene/settings and tracer
state. Production performance should also be checked with tracing disabled.

The first DLL build encountered three pre-existing zero-byte CommonLib objects
and one truncated COFF string table, all predating this work. Their original
files were preserved and only those intermediates were regenerated. The
environment doctor reports zero failures and one existing remote-URL warning.

Raw evidence remains local under
`build/validation/grass-lifetime-fix-20260925/`; the full crash capture remains
under `build/validation/simple-coc-fullcapture-25x5s-20260925T184610Z/`.
