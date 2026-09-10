# Upscaling and render-scale comparison reporting

Every update to the existing
[comparison ledger](vr-render-scale-comparison-ledger.csv) includes the
detailed comparison below automatically. Do this after measurements and
owned-capture cleanup, without adding work between measured transitions.
The user decides whether to include this analysis in a PR. Do not publish
it automatically or make it a PR requirement or merge gate. The separate
release-qualification protocol retains its own existing requirements.

## Separate results from assessment

Never use one PASS/FAIL label to summarize everything:

| Dimension           | Meaning                                                                                   |
| ------------------- | ----------------------------------------------------------------------------------------- |
| Execution           | COMPLETE, INCOMPLETE, or INTERRUPTED; counts and missing segments                         |
| Terminal result     | Original per-transition render/waiter result, explicitly scoped to the terminal condition |
| Task 2              | Original per-transition classifications and counts; no aggregate verdict                  |
| Full-history health | Every observed failure, recovery, retry, applicable gate failure, and evidence gap        |
| Change assessment   | IMPROVEMENT_SUPPORTED, NEUTRAL_SUPPORTED, DOES_NOT_MEET_STANDARD, or INCONCLUSIVE         |
| Reporting           | Whether the required evidence and analysis are complete                                   |

`DOES_NOT_MEET_STANDARD` means that the observed result does not support an
improvement-or-neutral assessment. It does **not** mean the test failed to
run. Preserve actual failed terminal results too: a later recovery must not
erase them. A terminal PASS does not erase recovered fidelity mismatches,
vendor fallback, lifecycle failures, or retries within the measured window.

Do not use the fixed two-frame stretch cutoff as a health or improvement
gate when settling imposes the stretch. Preserve the producer's raw gate,
label it `DIAGNOSTIC_ONLY`, and compare the measured episode count, total
frames and duration instead. Likewise, a scaled-presentation gate that
rejects `NativeOriginal` after a **proven** native-AA target is a labeled
`CONTRACT_MISMATCH`; retain its observed values and native both-eye proof.
Do not apply that exception without the exact native terminal evidence.
Other failed gates remain applicable unless their contract is separately
shown to be inapplicable. Report new, persistent, and resolved findings.

## Automatic workflow at each ledger update

1. Pin the user-selected reference. Otherwise select the previous relevant
   measured main-VR run from the canonical ledger, not the current checkout
   or an unrelated newer run. State the selection. Keep any specifically
   requested older renderer baseline as an additional comparison.
2. Preserve immutable raw evidence and verify each producer, Build ID,
   compiled source, stress session, request, epoch and indexed receipt hash.
   Record renderer base and exact main-VR base/equivalence separately from
   the compiled source when a reporting bridge was backported. Supply a
   provenance file with the source audit or Git evidence; unknown is `n/a`.
3. Prepare the ledger update from retained measurements. Use the repository
   wrapper below once to finalize the candidate when needed, validate and
   apply the prepared update, and generate the comparison. The installed plugin may
   lag the maintained source; use the explicit toolkit source path until
   its release is installed. Do not rotate a plugin during another run.
4. Update the existing ledger in place with every measured timing, including
   interrupted segments. Never replace historical cells or create another
   ledger. The same wrapper invocation verifies all selected numeric timing
   cells and historical-cell preservation before publishing a prepared update.
   If the ledger is already updated, omit the update arguments and audit it
   once. Do not generate a throwaway comparison before updating the ledger.
5. Add the compact per-pass assessment, affected routes, complete detailed
   side-by-side tables and evidence links to the durable run report and
   [iteration record](vr-render-scale-iteration.md). Do not finish a ledger
   update with only averages, a terminal PASS count, or a local file link.
   Keep unavailable data and unmatched runs explicit; retain useful partial
   comparisons without inventing timings or a neutral result.

```powershell
python tools/compare-render-scale-ledger.py `
  --toolkit-root <automation-source-or-package-root> `
  --node <node-executable> `
  --baseline-root <preserved-baseline-run> `
  --candidate-root <preserved-candidate-run> `
  --provenance-path <comparison-provenance.json> `
  --output-root <new-local-comparison-directory>
```

Without the optional finalization and ledger-update arguments, the wrapper
is read-only with respect to runs and the canonical ledger.
Its generated Markdown, JSON and CSV contain both builds, every pass and
transition, unmatched entries, full gate observations, and telemetry. Keep
raw trees local. The comparison export supplements the existing ledger.

### Fast routine completion

For the cross-machine reporting update, fetch branch
`codex/fast-render-scale-reporting-20260910` from `origin` in both
`skyrim-community-shaders` and `skyrim-vr-automation`. Use each checkout's
`tools/git.ps1` wrapper to fetch and check out the branch. Point
`--toolkit-root` at the automation source checkout. This source workflow
does not require a plugin reinstall; raw run directories remain local and
must be supplied separately when reproducing an old comparison.

Keep the same output directory when repeating a command. The wrapper checks
content hashes of all raw inputs, summaries/indexes, provenance, policy,
toolkit JavaScript and protocol matrices, its own implementation, Node, and
generated outputs.
Unchanged comparisons are reused; changed or missing inputs/outputs force
regeneration. Ledger timing validation always runs, using one metric index
instead of rescanning the whole CSV for each timing. Cache validity is not
based on timestamps, an old PASS label, or file size alone.

For a newly completed candidate, add `--finalize-candidate <request.json>`.
The request contains `variant`, `runId`, `buildId`, and `expectedRows`, with
optional `artifactPath`, `manifestPath`, and `generatedUtc`. Supply the exact
physical DLL and adjacent manifest paths for deployment verification when
needed. When present, worker state must be terminal, cleanup verified, and
pending evidence zero. Finalization writes to that candidate directory;
historical evidence replay must use a separate copy. Only unchanged inputs,
deployment files, and all five hashed finalizer outputs permit reuse. This
includes the full scalar CSV: no values or journal revisions are dropped.

For a prepared ledger update, also pass `--ledger-candidate <prepared.csv>`
and `--expected-ledger-sha256 <hash-before-preparation>`. The prepared file is
transaction staging, not a second comparison ledger. The wrapper validates
the paired timing cells and preserves every old cell before atomically
updating the existing canonical ledger. It retains a pre-update snapshot in
the output directory. Unexpected ledger changes or competing ownership fail
closed; inspect a retained lock before recovery. These options remove the
need to generate and audit the comparison twice.

`reporting-performance.json` retains stage durations, reuse decisions,
classification, and any failure; `ledger-validation.json` retains the exact
timing audit. Reporting limitations from finalization remain explicit and
separate from the ledger audit result. `--quiet` suppresses routine stdout,
not error exits or recorded findings. Brief useful progress lines are fine;
avoid unnecessary narration, polling, repeated extraction, tool maintenance,
packaging, regression suites, or ad-hoc report rewriting during an ordinary
run. Revisit checks only for changed inputs/code, failures or unresolved
concerns. Never omit full evidence, comparison tables, or required checks to
meet a time target. This command prepares local reports; it does not publish
to PRs or replace the required durable report/iteration update.

The provenance file has `baseline` and `candidate` objects, each containing
`sourceCommit`, `rendererBaseCommit`, `mainVRBaseCommit`, and `evidence`.
Use full hashes and cite the verification/audit; never infer an equivalent
main-VR commit from a branch name. The tool checks the compiled source
against the retained manifest and producer receipts.

## Required per-pass and per-transition detail

-   Exact run, compiled source, renderer base, main-VR base, Build ID, DLL
    hash/size and bridge backport; protocol/matrix, backend and dimensions.
-   Strict, presentation, cleanup and tail milliseconds; phase durations;
    absolute and percentage changes for every paired route in each pass.
-   Qualification-dispatch-to-strict frames, producer-request-to-applied
    relatch frames, and their timing origins. Keep these distinct. Show total
    stretch episodes, frames and milliseconds, recovery and any active tail.
-   Retry counts and reasons, request/epoch ownership, observed wait and
    stabilization intervals, and missing/overwritten retry evidence. A
    coalesced event cannot supply individual retry timestamps. Do not sum
    overlapping intervals or call the whole switch isolated retry overhead.
-   Device loss, OOM, terminal and lifecycle failures, fidelity mismatches,
    vendor/bounds fallback, phase violations, memory trim/retirement failures,
    and native backend evidence. Preserve all observed counters, including
    recovered events. Observations are not automatically unique frames.
-   Exact cumulative gate result, observed value, limit, applicability and
    reason. An inapplicable threshold remains visible without deciding health.
-   Per-pass totals, mean, median, p95 and maximum; worst affected routes and
    whether the direction repeats. Do not let a lower mean hide regressions.
-   Memory boundaries, deltas, pressure, resource lifetime/retirement and
    profiler evidence. Unresolved zero timer totals are unavailable timings;
    whole-frame GPU/FPS claims require fresh resolved samples and a matched
    scene/frame budget. Missing evidence is never zero cost or leak freedom.
-   Scene, timing, weather, adapter/driver, headset, fixture, modlist/cache,
    build/toolchain, pacing and measurement coverage differences. Separate
    worker/reporting delay from producer latency and health.

## Improvement and neutral claims

Always show observed deltas. Formal improvement or neutrality additionally
requires matching fixture evidence, repeat coverage, complete relevant
health evidence and an explicit versioned tolerance policy. Do not invent
a tolerance or claim significance from one process with two ordered passes.
The optional `--policy-path` supplies `id`, `absoluteToleranceMs`,
`relativeTolerancePercent`, and `requiredPasses` (at least two). Its identity
and values are embedded in the output. A route exceeds tolerance only when
it exceeds both the absolute and relative tolerances.

New adverse health findings prevent an improvement-or-neutral assessment
even when the average switch is faster. Without comparable evidence or a
declared tolerance, timing results are descriptive and the assessment is
inconclusive, unless concrete adverse health findings already demonstrate
that the standard is not met. Existing inapplicable stretch/native-target
gates are not evidence of a newly introduced regression.
