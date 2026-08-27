# RC166 Lessons Stacked-PR Workflow

This file governs cumulative work on
`codex/vr-renderscale-RC166lessons-integrated-v2`. Read it together with every
applicable `AGENTS.md` and the repository's existing AI/PR documents before
changing code.

## Scope and series order

The branch starts from the complete current `main-VR`, including the merged
PR49 OpenVR Submit resource lease. Add the remaining RC166 lessons in this
order:

1. Qualification milestones (`presentationStable` versus `cleanupDrained`)
2. Render-scale memory high-water measurement
3. Replacement-mutation versus current-presentation audit/hardening
4. Render-scale authority/liveness map and executable invariants
5. Render-target publication dependency/timing audit
6. Never-submitted generation retirement provenance
7. Hidden-area-mask dispatch-admission audit
8. Render-scale preparation timing and safe asynchronous staging
9. PR43 GPU-work regression guards
10. Reverted PR47/PR48 authority-design guardrails

The former temporal depth-culling measurement item is explicitly excluded. The
current temporal recovery implementation helped recover performance and must not
be changed or instrumented by this series without new runtime evidence.

Complete and adversarially review each concern before beginning the next. Keep
separable implementation, diagnostics, tests, and documentation in separate
commits. The branch is cumulative: its final head must contain current
`main-VR`, PR49, and every accepted concern above in order.

## Before editing

- Inspect the current branch, base SHA, working tree and applicable instructions.
- Preserve unrelated user work. Never reset, clean, stash, overwrite or rewrite it.
- Read relevant implementation, tests, commit rationale, merged/closed/reverted PRs and upstream references before changing behaviour.
- Reassess the proposed problem against current code. Do not manufacture a production change when evidence supports only tests, documentation or measurement.
- State the exact invariant and failure behaviour before implementation.

## Commit structure

Address separable concerns as separate commits. Do not combine implementation, diagnostics, tests and documentation merely for convenience when they can be reviewed independently.

Commit subject format:

`scope(target): imperative summary`

Examples:

- `fix(vr): lease OpenVR submit resources`
- `build(devbench): expose submit lease invalidation`
- `test(vr): cover stale submit publications`
- `docs(upscaling): document submit lease guarantees`

Every non-trivial commit body must contain, as applicable:

- **Rationale:** the concrete failure mode or missing evidence;
- **Implementation:** what changed and why this design was selected;
- **Safety/fidelity:** preserved contracts and fail-closed behaviour;
- **Validation:** exact tests/builds actually run, without claiming unrun validation;
- **Provenance/acknowledgement:** identify code copied, adapted from, or materially guided by another implementation.

For external guidance or adaptation, include the source repository/PR/commit and the contributor's GitHub noreply email when it can be established reliably, for example:

`Adapted-from: https://github.com/OWNER/REPO/pull/123`

`Source-commit: <sha>`

`Co-authored-by: Name <id+login@users.noreply.github.com>`

Do not add a co-author trailer merely for conceptual inspiration. When no external code was copied or directly adapted, say so in the PR provenance section when useful.

## Implementation principles

- Prefer one authoritative owner over duplicated flags, hints, caches or epochs.
- A diagnostic/cache/wake hint must never suppress service of authoritative work.
- Preserve the last truthful, resource-proven presentation until physical mutation makes it invalid.
- Device loss, stale resource identity, wrong generation, unresolved physical mutation and unproven stereo remain fail-closed.
- Avoid churn, opportunistic refactors and unrelated formatting.
- Keep non-VR behaviour unchanged unless explicitly in scope.
- Preserve visual fidelity, eye-local geometry, resource lifetime, provider contracts, save/load behaviour and compositor continuity.
- Do not weaken memory, retirement, presentation or provider safety merely to improve a benchmark.
- Use bounded storage and saturating arithmetic for diagnostics.
- Avoid detached threads and unbounded queues.

## DevBench and logging

Every PR must assess whether additional evidence is required for in-game evaluation.

- Prefer structured DevBench measurements behind `DEVBENCH_BRIDGE_ENABLED`.
- Bind sessions and observations to exact build, session, request, transition epoch, contract generation, device and loading serial where relevant.
- Production builds must not pay OS queries, QPC calls, allocations, locks or verbose logging solely for diagnostics.
- An inactive production path should compile out or reduce to at most one predictable cheap check when unavoidable.
- Debug logging must be rate-limited or transition-scoped, avoid per-frame spam and never become behavioural authority.
- Diagnostics are observational. They must not create, clear, gate or retire production ownership.

## Testing and validation

Use the repository's canonical commands from its AI/development documents.

At minimum, where applicable:

- focused policy/controller tests;
- static bridge/contract tests;
- shader tests for HLSL or binding changes;
- full `ControllerTests` label;
- Release DLL build;
- pinned formatting checks;
- `git diff --check` / `git log --check` over the series range.

Do not state that a build, runtime test or headset test passed unless it actually ran and passed.

## Mandatory adversarial review before opening/updating a PR

After all intended commits are present, review the complete base-to-head diff as a hostile reviewer.

Check:

- exact scope and absence of unrelated churn;
- authority and publication ordering;
- lock ordering and external-call lock scope;
- stale owner/generation/device/session races;
- lifetime and use-after-free risks;
- first-eye/second-eye and partial-stereo paths;
- save/load, menu, startup, native restore and device-loss paths;
- retry, timeout, cancellation, supersession and wraparound;
- memory-pressure and allocation-failure behaviour;
- non-VR compile/runtime behaviour;
- fidelity and geometry preservation;
- diagnostics overhead when disabled and boundedness when enabled;
- DRY implementation without obscuring safety-critical distinctions;
- pitfalls documented by earlier failed/reverted implementations.

Fix findings in appropriately scoped commits rather than appending an unexplained catch-all change. Re-run affected validation after review.

## Pull-request format

Follow the repository's existing PR template and AI documents exactly. The PR body must make review possible without reading commit titles alone and should contain, as applicable:

- Why / concrete failure or evidence gap;
- What changed, grouped by commit or concern;
- Invariants and safety/failure behaviour;
- DevBench/diagnostic evidence added and production-overhead statement;
- Validation with exact commands and results;
- Runtime/headset validation not yet performed;
- Provenance and acknowledgements;
- Stacked-series base/head relationship and dependencies.

Do not overclaim performance. Distinguish measurement infrastructure, offline validation and actual in-game evidence.
