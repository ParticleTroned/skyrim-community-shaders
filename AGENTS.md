# Agent instructions

This is the canonical repository-wide policy for coding agents. It applies to
the complete repository. GPT/Codex and Codex Code Review discover this file
directly. `.claude/CLAUDE.md` contains the detailed architecture and build
reference and imports this file. `AI-INSTRUCTIONS.md` and
`.github/copilot-instructions.md` are tool-specific entry points and must not
contradict this policy.

## Quick checklist

-   **PR identity:** Format PR titles as `type(scope): description (#<number>)`. A branch created for an already-numbered PR must contain `pr<number>`; do not rename an open PR's head merely to retrofit the number.
-   **PR title:** Keep the descriptive portion at or below 50 characters when practical, target `main-VR`, and keep the title current because squash merge and release automation consume it.
-   **PR body:** Wrap prose at 72 columns where practical and use `Why`, `What changed`, applicable safety/failure behavior, and exact validation evidence. Update stale text before merge.
-   **Release-aware type:** Use `feat`, `fix`, or `perf` only for user-visible release changes. Developer tooling and build infrastructure are `build`; CI is `ci`; documentation and agent guidance are `docs`.
-   **Commits:** Use the same Conventional Commit format. Every agent-created or rewritten commit must have a wrapped body with explicit `Rationale:` and `Implementation:` sections and accurate attribution. Stage only in-scope files.
-   **Comments:** Keep inline comments to one or two lines. Explain why, not what. Describe present invariants, not removed code, one-off incidents, commits, PRs, or tools.
-   **Minimal churn:** Do not reformat unrelated code, rename adjacent symbols, or mix opportunistic cleanup into the requested change.
-   **DRY review:** Search the full codebase for an existing utility or pattern before adding another implementation.
-   **Complete work:** Do not ship TODO, FIXME, placeholders, swallowed errors, or known partial implementations unless the user explicitly requests a plan or scaffold.
-   **Runtime safety:** Evaluate SE, AE, and VR behavior. Keep runtime-specific divergence small, explicit, and localized.
-   **Graphics safety:** Name every new D3D11 resource with the existing `Util::SetResourceName` path and use RAII for graphics and ImGui state.
-   **Validation:** Test in proportion to risk, record exact evidence, and never claim validation that did not run.
-   **Git safety:** Never force-push or rebase shared branches. Preserve user changes, build outputs, and shader caches.

## Code Review Rules

### Runtime and render safety

-   Flag shared C++ or HLSL changes that assume one Skyrim runtime. Safe path: use the established runtime detection, relocation, cached accessor, and `VR` permutation patterns with the divergence kept local.
-   Flag a new D3D11 resource without the repository's resource-name path, or state mutation without deterministic restoration. Safe path: use `Util::SetResourceName` or the named wrappers and RAII for resource and state ownership.
-   Flag user-controlled shader dimensions, counts, paths, or ranges that reach allocation or dispatch unchecked. Safe path: validate at the configuration boundary and fail closed or retain the previous valid state.

### Integration contracts

-   Flag a new runtime feature or settings surface that cannot be exercised through DevBench. Safe path: add or update the action, description, and schema in the same PR.
-   Flag build, CI, documentation, or internal-tooling changes labeled as a user-visible `fix`, `feat`, or `perf`. Safe path: select the release-neutral Conventional Commit type that matches the final diff.

## Pull requests and commits

### Target and title

-   Target the repository's integration branch, currently `main-VR`. Do not target `main`, `dev`, or another release line unless the user explicitly directs it.
-   Use `type(scope): description (#<number>)` for GitHub PR titles and `type(scope): description` for commits. The scope should identify the affected domain, such as `shaders`, `water`, `vr`, `tooling`, `build`, or `ci`.
-   Every local and remote branch created for work on an already-numbered PR must contain its lowercase `pr<number>` identity, for example `codex/pr58-focused-forward-port`. A new PR needs a stable descriptive head because GitHub assigns its number only after creation; append the assigned number to its title immediately. Never rename or delete an open PR's head solely to retrofit its number because GitHub closes the PR instead of retargeting it.
-   Use an imperative, specific description. Keep titles at or below 50 characters when practical; never shorten them into ambiguity merely to satisfy the limit.
-   Re-evaluate the PR title after every material scope change. Correct stale metadata with `gh pr edit <number> --title "..." --body-file <file>` before merge.
-   The PR title becomes the squash commit and drives semantic-release behavior. Select the type for its release effect, not for rhetorical emphasis.

### Type and release impact

| Type       | Use for                                        | Release impact |
| ---------- | ---------------------------------------------- | -------------- |
| `feat`     | New user-visible capability                    | minor          |
| `fix`      | User-visible defect correction                 | patch          |
| `perf`     | Measured user-visible runtime performance gain | patch          |
| `revert`   | Revert of an earlier change                    | inherited      |
| `build`    | Build system, packaging, dependencies, tooling | none           |
| `ci`       | Workflows and automation                       | none           |
| `docs`     | Documentation, comments, and agent guidance    | none           |
| `refactor` | Code restructuring without behavior change     | none           |
| `style`    | Formatting-only changes                        | none           |
| `test`     | Tests and test infrastructure                  | none           |
| `chore`    | Maintenance when no more specific type applies | none           |

-   Do not label build, CI, test, or internal tooling work as `fix`; that consumes a patch release for a non-user-visible change.
-   Do not label a refactor as `feat` or an unmeasured internal optimization as `perf`.
-   Append `!` or use a `BREAKING CHANGE:` footer only for an intentional breaking change.

### PR body

-   Start with `## Why`: describe the current problem, user/developer impact, and relevant constraints.
-   Use `## What changed`: describe observable behavior and architectural decisions, not a file-by-file diff transcript.
-   Add a focused section for runtime behavior, compatibility, safety, migration, or failure behavior when the change needs it.
-   End with `## Validation`: list exact commands, tests, runtime scenarios, measurements, and results. Distinguish passed, not run, and blocked checks.
-   Wrap prose at 72 columns where practical. Do not break commands, paths, tables, identifiers, or URLs solely to meet the column target.
-   Keep the body synchronized with the final diff. Remove abandoned plans and include material follow-up fixes made during review.

### Commit hygiene

-   Commit only files required by the requested change. Leave unrelated tracked changes and untracked user files untouched.
-   Every commit created or rewritten by an agent must use this structure, even when the change is small:

    ```text
    type(scope): imperative summary

    Rationale:
    Explain why the change is needed and the constraint or impact it addresses.

    Implementation:
    Explain what changed, how it works, and material safety or failure behavior.
    ```

-   Keep each section specific to the final diff and wrap its prose at 72 columns where practical. Do not substitute a change list for the rationale or omit implementation details because a commit is small.
-   Preserve accurate Git authorship. Before creating a commit, inspect the configured author and committer identities; do not invent or silently replace either identity. When rewriting a commit, preserve its existing author name, email, and author date unless the user explicitly directs a correction.
-   Preserve original author metadata when carrying an existing commit through a merge or cherry-pick. When manually porting another person's material contribution, add their verified identity with a `Co-authored-by: Name <email>` trailer instead of claiming sole authorship.
-   Add co-author trailers only for people who materially authored the committed work. Do not invent names or email addresses, and do not credit reviewers, tools, or assistants merely for reviewing, generating, or applying a change.
-   Keep mechanical formatting separate from behavioral changes when that materially improves reviewability.

## Comments and documentation

-   Public declarations and API methods should have concise Doxygen documentation, especially for graphics-facing behavior and non-obvious contracts.
-   Inline comments should explain a constraint, invariant, safety condition, or surprising choice. Do not paraphrase the following statements.
-   Do not leave comments that refer to a commit, PR, temporary debugging incident, or a tool session. State the durable invariant instead.
-   Describe the code that exists. Mention removed/absent code only when a regression-risk warning is necessary to prevent a known unsafe restoration.
-   Update instructions and user/developer documentation in the same PR as the behavior they govern.

## Code quality and architecture

-   Prefer complete, focused changes with explicit error handling and graceful degradation.
-   Use descriptive domain names rather than unexplained abbreviations. Keep each feature and helper responsible for one coherent technique or policy.
-   Break functions approaching roughly 200 lines into focused helpers when doing so clarifies state ownership and control flow. Do not split merely to satisfy a number.
-   Centralize durable constants and UI theme values instead of repeating magic numbers.
-   Reuse utilities under `src/Utils/` for serialization, formatting, file paths, game settings, UI, and D3D behavior. Reuse cached `globals::game::*` accessors instead of introducing parallel singleton lookups when the cached accessor is valid at that lifecycle point.
-   Include what is used. Prefer forward declarations in headers and full includes in implementation files where practical.
-   Use callbacks or narrow interfaces between UI components rather than widening private implementation APIs.
-   Pair every successful `ImGui::BeginTable()` with `ImGui::EndTable()` and use RAII for ImGui style/state changes.

## DirectX, shaders, and runtime compatibility

-   Name every new D3D11 resource for RenderDoc using `Util::SetResourceName` after raw `device->Create*` calls. For `Texture2D`, `Buffer`, and `ConstantBuffer` wrappers, pass the name to the wrapper so view names remain centralized.
-   Use `Feature::Resource` naming for resources and the existing SRV/UAV suffix conventions for views. Do not duplicate the resource-naming GUID or reimplement the helper inline.
-   Manage graphics resources with RAII and restore modified DirectX state. Shader or DirectX failures must disable or fall back cleanly rather than crash or corrupt subsequent passes.
-   Instrument render-pass entry points with `CS_GPU_PASS`, using the dynamic-name form only when the label is not static. Keep raw Tracy or annotation zones for sub-dispatch detail that does not belong in the profiler table.
-   Validate user-controlled shader parameters, buffer sizes, texture sizes, counts, paths, and configuration ranges before allocating or dispatching.
-   Check register/buffer conflicts when adding shader resources. Consider render resolution, VR frame budgets, thread affinity, and render-thread ownership.
-   A genuinely new concern shared under `package/Shaders/Common/` should default to a focused new `.hlsli` file instead of growing an unrelated shared header. This limits validation fan-out and merge conflicts; it does not require retroactively splitting existing headers.
-   Universal code must consider SE, AE, and VR. Use the established CommonLib relocation/accessor patterns and runtime checks; keep VR-only branches small and localized.
-   For HLSL, use the existing `VR` permutation checks. For shared C++, prefer runtime detection and existing cached state, except where early initialization requires direct module detection.
-   A performance claim requires comparable measurements in a controlled scene. Express the result relative to the relevant frame budget; otherwise use `refactor` rather than `perf`.

## Validation and long-running work

-   Match validation to the changed surface: focused controller tests for policies, shader validation for HLSL, parser/unit tests for tooling, and runtime testing for UI/render/cache behavior.
-   For shader refactors expected to be behavior-preserving, use `tools/verify-shader-refactor.ps1` first. Identical DXBC is the preferred proof; otherwise use controlled runtime A/B evidence.
-   Runtime-affecting changes should be exercised through the available DevBench automation for each affected runtime. A new feature or settings surface should expose a DevBench action in the same PR. Changes to an exposed tool/action must update its registered description and schema in the same PR.
-   A PR that changes VR render-scale code or behavior must include the generated
    `csx-render-scale-pr-v1` summary described in
    `docs/development/render-scale-pr-qualification.md`. Preserve the complete
    evidence directory; missing or inconclusive unattended visual review, or an
    unmatched performance baseline, is not a pass. Human completion is not part
    of this protocol.
-   Scope pre-commit to staged files or the changed revision range. Do not use `--all-files` merely to validate a focused change; legacy third-party files preserve intentional formatting.
-   Never interrupt shader compilation or cache generation because output is temporarily silent. Check process and cache activity and allow the documented build window.
-   Preserve user-owned build outputs and shader caches unless the task explicitly requires their removal or regeneration.
-   Report exact passed, failed, skipped, or blocked checks. Do not turn a warning into a pass or omit a known validation limitation.

## Repository tooling

-   Run `pwsh ./tools/setup-dev.ps1` after cloning or when the developer-tool environment changes.
-   In Codex on Windows, invoke repository Git through `pwsh ./tools/git.ps1 <git arguments>` so linked-worktree ownership is scoped without changing global `safe.directory`.
-   Invoke CMake through `pwsh ./tools/cmake.ps1 <cmake arguments>` and pre-commit through `pwsh ./tools/pre-commit.ps1 run <arguments>`.
-   Run `pwsh ./tools/dev-doctor.ps1 -Network` when Git, hooks, authentication, caches, or the Windows sandbox behave unexpectedly.
-   Do not set user-level `TEMP` or `TMP`, and do not inject them with Codex `shell_environment_policy`; the launchers set writable paths only after the sandbox starts.
-   Use explicit SSH URLs for authenticated GitHub remotes and HTTPS for public dependencies. Do not globally rewrite all `https://github.com/` URLs to SSH. Use `pwsh ./tools/setup-git-user.ps1` for push-only SSH routing.
-   Git push authentication and GitHub CLI API authentication are separate. Never store an OAuth token in plaintext to bridge the isolated credential boundary.

## Git and release safety

-   Never push directly to, force-push, or rebase shared branches such as `main`, `main-VR`, `dev`, or `hotfix/*` without explicit user direction. Use `--force-with-lease` only when rewriting an owned feature branch is necessary and authorized.
-   Do not manually create `v*` release tags or hand-edit the CMake project version; release automation owns them.
-   Synchronize upstream histories by merge rather than cherry-picking individual commits. Preserve VR-specific behavior during conflict resolution and verify upstream ancestry after the merge.
-   Do not squash upstream-sync PRs when the merge ancestry is itself part of the synchronization contract.

## Maintaining these instructions

-   Update this file in the same PR that changes a convention; do not leave instruction drift for a follow-up.
-   Keep `AGENTS.md` canonical. Tool-specific instruction files should import or point here and contain only tool-specific additions.
-   Prefer a focused link into `docs/development/` when a topic needs more than a short policy summary.
-   Periodically remove stale absolutes that no longer match repository behavior, and verify all named tools, paths, branches, and APIs still exist.
