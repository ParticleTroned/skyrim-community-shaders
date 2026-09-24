# Shadow admission fast path

This change reduces per-submission ownership bookkeeping on top of main-VR
`5b1321273`, retaining the three previously reviewed CPU optimizations.
It targets the remaining shadow accumulation lead from the RC166 logging
comparison. It does not establish that the bad-view bottleneck is resolved.

## Implementation and safety

The duplicate key uses eight initialized 64-bit words. Pass identity, shader,
property, geometry, material, property flags, technique, accumulation hint,
extra parameter, LOD mode, unknown draw-state fields and sorted registration
remain part of equality. Disjoint smaller fields share words without losing
bits. The existing unordered-dense byte hasher consumes this contiguous key
directly, avoiding the tuple hasher's temporary buffer. Hash collisions still
require full key equality and cannot suppress distinct submissions.

Native bucket refresh and admission share one bucket lookup under the
existing recursive mutex. The bucket's last-lookup cache already made the
second lookup cheap; eliminating it alone is not the performance rationale.
Only occupied buckets probe native emptiness, and that probe still runs for
every applicable registration. No native pointer or lifetime result is cached
across registrations.

The separate refresh API is removed; admission owns the occupied-bucket
probe and refresh decision in one place. Sparse-bucket tests exercise this
same combined path.

Owned pass copies and geometry/property references remain. Native reset,
partial drain, nested readers, concurrent producer serialization and
duplicate prevention retain their existing behavior. Preparation/allocation
failures cannot publish a partial submission; refreshed retired records are
reclaimed outside the lock, including after failed admission. Pools remain
reusable without warm-loop allocations.

The installer remains VR-only and leaves SE/AE untouched. No renderer
settings, shader assets, additional Info logging or DevBench actions change.

## Adversarial review

Review of `57c9200fa` found no production correctness defect in the packed
key or combined admission. All original identity fields remain represented,
including the full LOD byte and high flag bits. The byte hasher reads only
initialized array elements, and ordinary equality resolves hash collisions.
Deferred owner release keeps input objects alive while the native bucket is
refreshed and the replacement is published. Allocation failure continues to
release retired owners outside the state lock. Runtime gating and native
hook installation are unchanged.

The review corrected three maintenance and validation issues:

-   The old standalone refresh API had no production callers but duplicated
    the combined operation's lifetime decision. It and its obsolete test
    usage are removed.
-   The hook mock represented LOD mode as a plain byte. It now uses the
    engine's seven-bit index and one-bit boolean structure; the scalar-bit
    tests modify object representations to cover every bit of that layout.
-   The native-drain failure test assumed the next deque insertion allocated.
    It now varies pool occupancy, checks both acceptance and failure, and
    requires an observed allocation failure without assuming one block size.

Additional regression coverage forces key hash collisions, verifies that
native drain/re-admission never transiently drops the last retained geometry
reference, and exercises probe and partial-preparation exceptions with both
automatic and explicitly deferred owner release. These changes add no
checks, allocations, or synchronization to the production registration path.

## Focused validation

Release tests with MSVC 19.51.36252.0 and `/W4 /WX /EHsc` pass:

-   `ShadowBatchSubmissions`: includes native refresh followed by duplicate
    admission, next-generation admission under a reader, and owner retirement;
    failed probes preserve membership, and partial preparation retires
    ownership safely in both release modes. Retains the 25,000-operation
    mixed-lifetime test.
-   `ShadowBatchHooks`: extracts the production hooks. New coverage changes
    every bit in scalar draw-state fields, each pointer identity, and sorted
    registration; each distinct state submits once and its duplicate remains
    suppressed, including deliberately colliding hashes. Native drain followed
    by allocation failure releases retired ownership and permits retry;
    successful replacement retains the owner continuously. Existing
    allocation-failure, renderer reuse, cross-thread and native callback tests
    remain.
-   `ShadowBatchInstall`: preserves runtime gating and transactional hook
    installation.

The submissions and hooks tests also pass with MSVC AddressSanitizer and the
compiler runtime directory on the process PATH. The original pre-review
evidence retains a failed sanitizer launch (0xc0000135) and its successful
retry separately; both reviewed sanitizer tests executed and passed.

## Controller benchmark

Baseline `5b1321273`, original `57c9200fa`, and reviewed code compile the same
updated hook-test harness. Measurements rotate all six process orders twice
for 12 runs per variant on one logical CPU. Each run reports the median of
nine warmed rounds of 30 registration/render/drain cycles. The table shows medians across runs.
Owners use atomic reference counts; native rendering is mocked. Times include
registration and drain, not real game rendering or GPU execution.

| Submissions | Interleaved groups | Baseline us/cycle | Original us/cycle | Reviewed us/cycle | Reviewed vs baseline |
| ----------: | -----------------: | ----------------: | ----------------: | ----------------: | -------------------: |
|          64 |                  1 |             4.075 |             3.910 |             3.917 |               -3.89% |
|         512 |                  1 |            33.508 |            31.788 |            31.953 |               -4.64% |
|        4096 |                  1 |           294.850 |           274.327 |           275.563 |               -6.54% |
|          64 |                 16 |             4.738 |             4.437 |             4.483 |               -5.38% |
|         512 |                 16 |            38.545 |            36.143 |            36.103 |               -6.33% |
|        4096 |                 16 |           347.927 |           329.578 |           328.830 |               -5.49% |

The original and reviewed executables have byte-identical `.text` sections
with the same harness (SHA-256
`5bc8a7a9d83f12583b8c2fa3e5ebefafab66a78f88c3f6a27de42b87304550e5`).
Deleting the unused template API adds no executed instructions. Their
measured differences, -0.23% to +1.05%, are run-to-run variation, not evidence
of an additional speedup or regression.

All measured warm loops allocate zero times. Native-only control timings are
retained in the raw results, including their variation. These controller
results justify a candidate for gameplay comparison, not a 4-7% frame-time
claim. At 4096 submissions the observed absolute saving is about 19 us
per mocked cycle; it must not be scaled to claim recovery of the earlier
multi-millisecond instrumented accumulation gap.

Reviewed evidence and the repeatable driver are in
`build/shadow-admission-review-20260925/benchmark.json` and `benchmark.py`;
`release.xml`, `comparison-tests.xml`, and `asan.xml` retain correctness
results. That directory also preserves the exact baseline/original sources,
original patch, and per-run output. The earlier AIO and its delivery receipt
under `build/shadow-admission-20260925` still identify the unamended original
commit and must not be labeled as the reviewed build.

Runtime visual, stability and frame-time qualification
remain outstanding; compare the exact same camera and light workload and
verify the DLSS eye path before interpreting a production A/B result.
