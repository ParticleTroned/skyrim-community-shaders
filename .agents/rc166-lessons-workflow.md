# RC166 Lessons Integration Workflow

This file governs work prepared on or above the cumulative RC166
render-scale test branch. Read it with the repository `AGENTS.md`,
`AI-INSTRUCTIONS.md`, `.claude/CLAUDE.md`, and PR template.

## Corrected series order

Apply and validate the original assessment items in this order:

1. Item 1: OpenVR Submit resource lease.
2. Item 4: separate presentation stability from cleanup drain.
3. Item 6: render-scale memory high-water measurement.
4. Item 2: replacement mutation versus current presentation.
5. Item 3: render-scale authority and liveness invariants.
6. Item 7: render-target publication dependency audit.
7. Item 8: never-submitted generation retirement provenance.
8. Item 9: hidden-area-mask dispatch admission audit.
9. Item 10: render-scale preparation timing and safe staging.
10. Item 11: PR43 GPU-work regression guards.
11. Item 12: reverted PR47/PR48 authority guardrails.

Original item 5, temporal depth-culling measurement, is excluded from
this render-scale stack. The restored bounded culling path recovered
part of the observed performance loss and must remain unchanged unless
later controlled evidence specifically implicates it.

## Commit and PR rules

- Address separable concerns as separate commits.
- Use `type(scope): imperative summary` subjects.
- Non-trivial commit bodies must describe rationale, implementation,
  safety/fidelity, exact validation, and provenance where applicable.
- Use `-x` or explicit source trailers when replaying existing commits.
- Credit copied or materially adapted external work with repository,
  source commit/PR, and an appropriate GitHub noreply co-author trailer.
- Avoid unrelated churn and preserve SE, AE, VR, visual-fidelity,
  resource-lifetime, provider, save/load, and compositor contracts.
- Perform a hostile base-to-head review for scope, races, ownership,
  locking, lifetime, stereo coherence, failure behavior, and DRY design.
- Do not manufacture a production change when evidence supports only
  measurement, tests, or documentation.

## DevBench and logging

- Put new runtime evidence behind the existing DevBench bridge.
- Bind observations to exact session/request/epoch/generation/device
  ownership where relevant.
- Diagnostics must never create, clear, gate, or retire production work.
- Production builds must not pay per-frame logging, QPC, OS queries,
  allocations, locks, or unbounded storage solely for diagnostics.
- Debug logging must be transition-scoped or rate-limited.

## Cumulative test branch

Each accepted item is replayed onto the cumulative test head only after
its own adversarial review. Empty topic branches are not treated as
implementations. The cumulative branch is for integrated build and
headset evaluation; it does not replace item-specific review evidence.
