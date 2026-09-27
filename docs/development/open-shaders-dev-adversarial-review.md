# Selective Open Shaders sync: adversarial review

Review baseline: `e5fa9a52468e01554163ebe645d9d98e38490806` on
`codex/pr730-open-shaders-dev-sync`. The scope includes the approved
runtime/build ports, their integration corrections, and interaction with
the merged Adaptive Balance Color base. Primary `main-VR` remains separate.
Original port commits are retained; each affected port receives a new
correction commit. Review and validation are in progress.

## Runtime payload installation: corrected

Affected port: `315f1d7a1` (#776). The install guard captured missing-file
state only at configuration. Removing or changing a previously verified
runtime DLL afterward allowed installation to enter its destructive AIO
staging reset before the missing file failed, or package changed bytes.

The guard now records configured payload SHA-256 values and checks the
selected components' current files before any install action. One shared
check handles FidelityFX and Streamline; SKSE-only installation remains
independent. Runtime downloads, cache ownership and deployment are unchanged.

The extended script fixture reproduced the old guard accepting a changed
FidelityFX DLL. It now passes changed/missing payload cases for both
providers, verifies staging preservation, permits SKSE-only installation,
and accepts restored valid files. The existing download, cleanup and
deployment-ownership cases also pass. Evidence under
`build/analysis/open-shaders-dev-review-20260926`:

-   `review-runtime-before-repro-final.log`: expected rejection missing.
-   `review-runtime-after.log`: complete policy fixture passed.
-   `run-runtime-review.ps1`: exact argument-array runner.

Three earlier command invocations failed at argument parsing, before the
regression ran. `review-dev-doctor.log` reports zero failures and one
existing public-HTTPS remote warning. No game deployment occurred.
