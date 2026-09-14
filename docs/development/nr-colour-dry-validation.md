# NR colour: dry-validation follow-up

Base: `a6697404a2b3045f4d3d49d8468ebf3bcd6fc5d0` on
`work/face-of-gogh-colour-managed-20260914`.

This change hardens the existing assessment and exposure-resource handling. It
adds no new NVIDIA parameters, colour codec, hook target or shader algorithm.
The local review used a partial, blob-verified source snapshot, not a complete
Windows checkout. No Skyrim process, live controller or NVIDIA DLL was used.

## Fixed and covered

* Sampling uses one monotonic deadline through the controller's operation/request
  limits and Python's subprocess envelope. Late responses are not evidence. The
  timeout is per sample phase; restoration has its own bounded operation budget.
* A healthy unchanged session that produces no qualifying measurements classifies
  just that candidate as unavailable. Transport/semantic errors, identity loss
  and configuration-ownership changes still terminate the campaign. All candidates
  unavailable is failure, not success.
* Each phase obtains a fresh ownership/status check before selecting its warm-up
  floor. Hidden-edit A/B waits for actual hidden-edit measurements and requires a
  baseline-equivalent result, not merely a configuration acknowledgement.
* Measurements require typed counters/flags and complete valid sample counts.
  A missing transport flag is not false. Known secondary regions cannot disappear
  just because the current observation advanced beyond a delayed readback.
* Readiness waits inherit the same runtime/artifact/workspace expectations as
  calls. Restoration verifies both the expected new revision and original editable
  values. An indeterminate write is never retried or blindly restored.
* Capture failures preserve both primary and cleanup errors. A lost start response
  does not authorize an automatic stop. Recovery-required capture state prevents
  further configuration changes; retain its session/journals for explicit recovery.
* `ExposureCapture::Bind` now rejects deferred contexts, unknown source frames and
  resources from another D3D device before copying or updating them. Existing
  latches cannot be reused through another context/device. Transaction admission
  is shared with the executable portable lifecycle tests in `ExposurePolicy.h`.
  Capture epoch changes still do not split an active stereo/ROI transaction;
  explicit reset/retirement clears the latch.
* Asset verification rejects locally present but unpackaged shader includes and
  reports unreadable source/runtime/deployed files as structured failures.

## Controlled pre-change reproductions

Three independent probes were executed against both the original `assess.py`
blob (`1ccd716d7e0034b83bf6b3e515c61dd56533d95d`) and the patched module:

| Probe | Original | Patched |
| --- | --- | --- |
| Status response after the sampling deadline | Accepted | Rejected |
| Known secondary region has a newer current observation but no matching sample | Primary-only group accepted | Group rejected |
| Missing transportBypass flag in an inference sample | Accepted as false | Rejected |

The GPU device/context issue is a source-level safety finding, not a reproduced
live D3D failure. The portable tests verify the production admission policy;
they do not execute `CopyResource`, engine hooks, COM ownership or GPU fences.

## Region completeness

API v2 does not publish an immutable per-transaction physical-region manifest.
Default collection therefore reports `regionCompleteness=retained_observations_only`.
For a prepared fixed multi-ROI fixture, supply its authoritative slot set, e.g.
`--expected-physical-slots 0 1 4 5` for that main-route layout, or the appropriate
submit-route slots. Do not assume this example is the slot set for every scene.
The option accepts one route and requires every specified slot. A dynamic ROI
can deliberately become unavailable under this conservative requirement.

A successful run ranks source-relative RGB drift, weighted by valid sample count.
It does not certify the model domain, material accuracy, real inference or GPU
performance. `domainVerified` remains false. Real NGX activity must also be
checked with existing NR evaluation telemetry during live qualification.

## Offline validation

Commands for a complete local checkout:

```powershell
cmake -S tests/neural_color -B build/nr-color-tests
cmake --build build/nr-color-tests --config Release
ctest --test-dir build/nr-color-tests -C Release --output-on-failure
python tools/nr-color/verify_assets.py
python tools/nr-color/assess.py --include-captured
```

During authoring, the following selected portable CTest targets passed:

* `NRColorPolicy`: 452 checks.
* `NRExposurePolicy`: 513 checks.
* `NRExposureLifecycle`: 88 admission/lifecycle checks.
* `NRColorRunnerWorkflow`: 30 process-double/evidence tests.
* `NRColorAssetVerifierFixtures`: 9 synthetic package/failure-path tests.

Eight existing `AssessmentTests` independent of missing full-checkout files also
passed. The original conflict-test double now accepts the deadline keyword.
Python syntax checks, strict-warning C++ lifecycle compilation and patch checks
passed. The patch was applied to a second clean copy of the original partial
snapshot and the five selected CTest targets were rerun successfully.

Not run: the full source-contract suite, a full repository/deployed-Data inventory,
Windows plugin compilation, WARP execution, live PowerShell/DevBench transport,
real exposure-hook timing, or Skyrim image/performance comparisons. The checked
asset fixtures are not evidence of the user's deployed package or MO2 winner.
The existing four colour compute shaders and common include are unchanged.

## Next gate

Build the plugin and run all tests in the complete Windows checkout, then check
actual shader deployment. Only then use a prepared static scene to qualify HDR
bindings/timing, transport bypass, true NR execution, A/B appearance, moving
stereo/ROI behaviour and cost. No production colour profile is chosen here.
