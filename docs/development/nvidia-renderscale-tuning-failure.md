# NVIDIA render-scale tuning failure

## Scope

The 2026-08-29 NVIDIA public-upscaling-API tuning run did not produce a
render-scale comparison. The 33-transition matrix was stopped by the operator
after transitions 1 through 3 failed qualification. Transitions 4 through 33
were not run, and the comparison ledger was not updated.

The interrupted run used:

-   build ID
    `9abcbe4c435bf45ad91de45683b4c05158f6abeb621536c7acba974054d0e050`;
-   source commit `8b191ec27f0f85c2e7e99d31e7c69d9db117995d`;
-   NVIDIA GeForce RTX 4090;
-   `WhiterunDragonsreach`; and
-   automation protocol version `0.8.0+codex.20260829065731`.

The local raw receipts were retained under
`.tmp/renderscale-tuning-nvidia/20260829T080239Z/` but are intentionally not
versioned. This compact report preserves the cross-machine conclusions. The
local directory contains one JSON receipt for each completed transition and
`interrupted-summary.json`, which marks every remaining matrix entry `NOT RUN`.

## What failed

All three public API apply operations were accepted and later reported
`completed` with `success`. Qualification still timed out on every row:

| Row | Target | Final public API profiles                                                        | Qualification failures                                                                                                                                       |
| --: | ------ | -------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ |
|   1 | None   | configured, requested, effective, and stable were None                           | `profile_mismatch`, `qualification_timeout`                                                                                                                  |
|   2 | TAA    | configured and effective were TAA; requested and stable remained None            | `stale_source_observation`, `profile_mismatch`, `native_presentation_not_stable`, `qualification_timeout`                                                    |
|   3 | DLAA   | configured and effective were DLSS Native AA; requested and stable remained None | `physical_active_contract_mismatch`, `both_eye_fidelity_not_stable`, `vendor_presentation_not_stable`, `vendor_lifecycle_not_clean`, `qualification_timeout` |

The failure has two independently observed parts.

First, `qualification_wait` did not decode the same method identity as the
direct `csx.upscaling` snapshot. Its embedded upscaling snapshot reported the
method as `unknown`, including for row 1 where the direct final snapshot was
coherently None. That mismatch explains why the otherwise coherent None row
failed `profile_mismatch`.

Second, the TAA and DLAA operation receipts reported successful completion
without converging every authoritative profile view. The direct snapshot
advanced `configured` and `effective`, but `requested` and `stable` remained
None. On the DLAA row, the qualification physical records also remained
inactive with backend None. The strict waiter therefore had no matching
active DLSS contract, both-eye fidelity publication, stable vendor
presentation, or clean DLSS lifecycle to accept.

This evidence establishes the failure mechanism seen by the assay. It does
not yet establish the code-level root cause. The leading boundary to inspect
is the shared method decoder and the point where a public operation changes
from accepted to completed relative to requested, stable, and physical
controller publication.

## Why the stop was safe

The failures were semantic qualification failures, not loss of controller
ownership. After each completed row, the public active operation ID was zero,
cleanup was drained, and the unresolved physical-mutation epoch was zero. The
DLSS trace captured zero duplicated-constants or evaluate failures. There was
no device-loss, out-of-memory, or terminal controller failure.

On interruption, the run stopped only its owned collectors. CPU session 1,
GPU capture frame 108144, stress session 2, texture-lifetime session 1, load
probe session 2, and profiler capture 1 were all inactive afterward. The
profiler was restored to its original disabled state, and every cleanup
receipt retained the same build ID.

## Required follow-up

1. Make `qualification_wait` consume the same method decoder and profile
   presence rules as the public upscaling service. Preserve the raw numeric
   method value when reporting an unknown enum.
2. Do not report a queued vendor operation as completed until its target is
   represented by the authoritative requested, effective, and stable views
   and the matching physical contract is published.
3. Define explicit convergence semantics for native None and TAA operations,
   which may not require an active vendor backend but must still expose a
   coherent requested/stable public profile.
4. Repeat the exact 33-transition NVIDIA matrix. Do not append a comparison
   ledger row unless every transition has one terminal qualification receipt
   and the full matrix completes.
