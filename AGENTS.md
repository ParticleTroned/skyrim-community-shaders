# Agent instructions

This is the canonical repository-wide policy for coding agents. It applies to
the complete repository. GPT/Codex and Codex Code Review discover this file
directly. `.claude/CLAUDE.md` contains the detailed architecture and build
reference and imports this file. `AI-INSTRUCTIONS.md` and
`.github/copilot-instructions.md` are tool-specific entry points and must not
contradict this policy.

## Quick checklist

-   **PR authorization:** Create a PR, including a draft, only when the user explicitly requests it for the change and names the repository and target base branch. Ask for any missing authorization or destination before publishing.
-   **PR identity:** Format PR titles as `type(scope): description (#<number>)`. A branch created for an already-numbered PR must contain `pr<number>`; do not rename an open PR's head merely to retrofit the number.
-   **PR title:** Keep the descriptive portion at or below 50 characters when practical and keep the title current because squash merge and release automation consume it.
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
-   **Git safety:** Create direct commits locally before pushing. Use the requested GitHub merge method for PRs, then synchronize the local destination branch. Never force-push or rebase shared branches. Preserve user changes, build outputs, and shader caches.

## Code Review Rules

### Runtime and render safety

-   Flag shared C++ or HLSL changes that assume one Skyrim runtime. Safe path: use the established runtime detection, relocation, cached accessor, and `VR` permutation patterns with the divergence kept local.
-   Flag a new D3D11 resource without the repository's resource-name path, or state mutation without deterministic restoration. Safe path: use `Util::SetResourceName` or the named wrappers and RAII for resource and state ownership.
-   Flag user-controlled shader dimensions, counts, paths, or ranges that reach allocation or dispatch unchecked. Safe path: validate at the configuration boundary and fail closed or retain the previous valid state.

### Integration contracts

-   Flag a new runtime feature or settings surface that cannot be exercised through DevBench. Safe path: add or update the action, description, and schema in the same PR.
-   Flag build, CI, documentation, or internal-tooling changes labeled as a user-visible `fix`, `feat`, or `perf`. Safe path: select the release-neutral Conventional Commit type that matches the final diff.

## Pull requests and commits

### Explicit authorization

-   Create a pull request, including a draft, only when the user explicitly requests that PR for the change and explicitly names its repository and target base branch. A request to "make a PR" without a target branch is incomplete; ask for the missing destination before publishing a branch or opening the PR.
-   Never infer PR creation or its destination from the checked-out branch, repository defaults, integration conventions, attached task documents, previous unrelated PRs, or completion of implementation and tests. Repository guidance describes how to prepare an authorized PR; it does not authorize one.
-   Authorization applies only to the named change and repository. A CSX PR request does not authorize a separate DevBench or automation PR, and a request for fixes or local commits does not authorize any PR.
-   Continue authorized local implementation, tests, builds, and review while PR authorization is incomplete. Keep the proposed work local until the user supplies the missing instruction.
-   Permission to create a PR does not authorize merging it, retargeting it, or pushing directly to a shared branch. Each requires a separate explicit user instruction identifying the affected PR or repository and branch.

### Target and title

-   Use only the target base branch explicitly named by the user. `main-VR` is the repository's integration branch, but is not an authorized default PR destination.
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
-   End with `## Validation`: summarize exact checks, runtime scenarios, measurements, and results; include commands only when portable and useful. Distinguish passed, not run, and blocked checks.
-   Wrap prose at 72 columns where practical. Do not break commands, paths, tables, identifiers, or URLs solely to meet the column target.
-   Keep the body synchronized with the final diff. Remove abandoned plans and include material follow-up fixes made during review.

### Commit hygiene

-   Commit only files required by the requested change. Leave unrelated tracked changes and untracked user files untouched.
-   Commit reusable build, fork, and shader guides with the changes they explain. Keep implementation notes, feature setup, investigations, run reports, and detailed validation provenance in ignored local documentation. Preserve exact measured source commits and Build IDs locally. Do not add those records to a PR or rewrite shared history to publish them.
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

-   Never publish machine-specific absolute paths or usernames in PR bodies,
    comments or committed documentation. Public validation summaries must
    also omit local evidence locations. Use repository-relative paths or
    generic placeholders only for reusable build and shader instructions.
-   Do not hardcode machine-specific filesystem paths in code, scripts,
    configuration, test fixtures, or documentation. Discover tool and
    installation locations or accept explicit configuration instead.
-   Keep `docs/development/` limited to reusable instructions for building
    shaders and working with this repository or a fork. Feature setup notes,
    fix explanations, investigations, handovers, run reports, and detailed
    provenance belong in `.local-docs/`, which must remain ignored by Git.
    Never force-add local documentation or attach its contents to a PR.
    This policy applies to every file format, including CSV ledgers, JSON
    summaries, text handovers, and supporting scripts in evidence folders.
-   Public declarations and API methods should have concise Doxygen documentation, especially for graphics-facing behavior and non-obvious contracts.
-   Inline comments should explain a constraint, invariant, safety condition, or surprising choice. Do not paraphrase the following statements.
-   Do not leave comments that refer to a commit, PR, temporary debugging incident, or a tool session. State the durable invariant instead.
-   Describe the code that exists. Mention removed/absent code only when a regression-risk warning is necessary to prevent a known unsafe restoration.
-   Update instructions and user/developer documentation in the same PR as the behavior they govern.

## Code quality and architecture

-   Prefer complete, focused changes with explicit error handling and graceful degradation.
-   Compile developer-only tracing and capture machinery out of production:
    use `TRACY_SUPPORT` for Tracy and `DEVBENCH_BRIDGE_ENABLED` for DevBench
    diagnostics. Runtime inactivity is not a substitute for build isolation.
    Preserve user-facing performance controls and timing readouts. Verify
    compiler output and forced headers, not just project definitions.
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
-   Unless an explicit test protocol requires another time, reset in-game
    comparisons to noon before every condition and separate measurement phase.
    Verify the observed `gameHour` is in `[12, 12.05]`, then settle for at
    least five seconds before capture. Preserve the reset and verification
    receipts; exclude night or mixed-time windows from the matched comparison.
-   The Astra depth-culling campaign compares native culling off, Legacy,
    Advanced and Hybrid in every repeated performance and stereo-visual
    condition. Legacy is required in every matched set.
-   VR render-scale changes still require the maintained qualification
    protocol. Keep its generated summary and complete evidence local.
    Missing or inconclusive unattended visual review, or an unmatched
    performance baseline, is not a pass; human completion does not replace
    the protocol. Public PRs contain a concise outcome and coverage summary.
-   Preserve render-scale ledgers, iteration notes, tuning reports, and
    failure summaries locally. Create immutable numbered snapshots for
    finalized measurements; never overwrite earlier records. Do not commit
    new measurement ledgers or reports, and do not rewrite shared history
    merely to fold local evidence into an implementation commit.
-   Local ledgers must preserve every available per-transition and per-pass
    timing, ordinal, route, definition, unit, and exact run/build identity.
    Retain interrupted repeats, retries, recoveries, cumulative health gates,
    observed values and limits, memory, resource and profiler details, and
    evidence gaps. Preserve false, zero, null, and empty values; distinguish
    missing receipts, failed measurements, and conditions not run. Audit
    every measurement and field against the complete saved summaries without
    changing historical cells. Aggregates and evidence links are insufficient.
-   Keep the detailed side-by-side comparison with those local records.
    Preserve exact compiled source, renderer base, main-VR equivalence, and
    Build IDs. Assess completion, full-history health, and improvement or
    neutrality separately. Retain actual completion frames and milliseconds,
    stretch frames and duration, comparison deltas, and recovered failures.
    A settling-related stretch cutoff remains a labeled diagnostic. Do not
    make publication of the detailed comparison a PR or merge requirement.
-   Use the maintained local reporting workflow once per finalized run.
    Reuse outputs only after matching evidence, source, deployment inputs,
    and hashes; always audit the timings. Preserve stage timings and missing
    data limitations. Do not add repeated polling, extraction, testing, or
    packaging without a new change, failure, or unresolved concern.
-   Scope pre-commit to staged files or the changed revision range. Do not use `--all-files` merely to validate a focused change; legacy third-party files preserve intentional formatting.
-   Never interrupt shader compilation or cache generation because output is temporarily silent. Check process and cache activity and allow the documented build window.
-   Preserve user-owned build outputs and shader caches unless the task explicitly requires their removal or regeneration.
-   Report exact passed, failed, skipped, or blocked checks. Do not turn a warning into a pass or omit a known validation limitation.
-   Finalize runtime DLL verification from the preserved producer Build ID and
    source commit. Resolve the exact enabled AIO mod and compare its physical
    `CommunityShaders.dll` SHA-256 and size with its adjacent
    `CSX.BuildManifest.json` and AIO build receipt. Match the manifest Build ID
    to the runtime producer and retain the compile identity. Reuse known AIO
    paths; use one bounded MO2 inspection only when needed. Check enabled loose
    providers, Overwrite, and unmanaged Data directly. Do not stop at MO2's
    virtual module path or recursively search game and build trees.
-   Keep worker lifecycle diagnostics separate from reporting completeness.
    Once owned captures are verified inactive, the complete evidence journal
    is flushed, required evidence is validated, and DLL identity is verified,
    a delayed helper shutdown or stale worker status does not make reporting
    incomplete. Preserve its diagnostic and ownership lock for later repair;
    do not wait indefinitely or replay measurements.

## Repository tooling

-   When the user invokes `gameft-sw`, follow
    the maintained automation protocol and its locally retained guide: ask
    its exact save-number question and
    use the maintained `skyrim-vr-automation/tools/gameft-sw` wrapper and
    versioned `game-ft` runner. Local ignored legacy copies remain historical.
    Stack/wait tracing is explicit and DevBench-only. Present timing and health
    before provenance or stack analysis; never silently change the base protocol.

-   Run `pwsh ./tools/setup-dev.ps1` after cloning or when the developer-tool environment changes.
-   In Codex on Windows, invoke repository Git through `pwsh ./tools/git.ps1 <git arguments>` so linked-worktree ownership is scoped without changing global `safe.directory`.
-   Invoke CMake through `pwsh ./tools/cmake.ps1 <cmake arguments>` and pre-commit through `pwsh ./tools/pre-commit.ps1 run <arguments>`.
-   Use `pwsh ./tools/validate-local.ps1` for the complete local DLL, controller, shader, and preset validation record. It builds both test groups and saves inventory, results, provenance, and full failure output under `build/validation/`.
-   Run `pwsh ./tools/dev-doctor.ps1 -Network` when Git, hooks, authentication, caches, or the Windows sandbox behave unexpectedly.
-   Do not set user-level `TEMP` or `TMP`, and do not inject them with Codex `shell_environment_policy`; the launchers set writable paths only after the sandbox starts.
-   Use explicit SSH URLs for authenticated GitHub remotes and HTTPS for public dependencies. Do not globally rewrite all `https://github.com/` URLs to SSH. Use `pwsh ./tools/setup-git-user.ps1` for push-only SSH routing.
-   Git push authentication and GitHub CLI API authentication are separate. Never store an OAuth token in plaintext to bridge the isolated credential boundary.

## Git and release safety

-   CSX public releases publish only the complete `CSX_AIO-*.7z` installer. Keep split core, feature and cache packages as internal workflow artifacts. Release notes must describe CSX's bundled features and built-in systems; never inherit upstream Nexus upload destinations. Follow [the CSX distribution contract](docs/development/csx-release-distribution.md).
-   Never push directly to, force-push, or rebase shared branches such as `main`, `main-VR`, `dev`, or `hotfix/*` without explicit user direction. Use `--force-with-lease` only when rewriting an owned feature branch is necessary and authorized.
-   For direct commits to this repository, create the commit locally and
    update the matching local destination branch before an authorized push.
    Integrate work from another branch or linked worktree by merge or
    fast-forward first. Never leave the local destination branch behind a
    direct remote update. Verify local and remote commit IDs afterward.
    A request to commit locally does not authorize a push.
-   Publish PRs through the normal GitHub workflow. Use the user's requested
    merge method on GitHub, then synchronize the local destination branch.
    Do not substitute a direct branch push for a normal PR merge unless the
    user explicitly requests that method. The direct-commit local-first rule
    does not govern normal PR merges or user-authorized contributions to an
    existing PR in another repository; follow that PR's workflow instead.
-   Do not manually create `v*` release tags or hand-edit the CMake project version; release automation owns them.
-   Synchronize upstream histories by merge rather than cherry-picking individual commits. Preserve VR-specific behavior during conflict resolution and verify upstream ancestry after the merge.
-   Do not squash upstream-sync PRs when the merge ancestry is itself part of the synchronization contract.

## Maintaining these instructions

-   Update this file in the same PR that changes a convention; do not leave instruction drift for a follow-up.
-   Keep `AGENTS.md` canonical. Tool-specific instruction files should import or point here and contain only tool-specific additions.
-   Link public build and shader guides from `docs/development/`. Keep feature-specific procedures and investigation details in the ignored local documentation.
-   Periodically remove stale absolutes that no longer match repository behavior, and verify all named tools, paths, branches, and APIs still exist.
