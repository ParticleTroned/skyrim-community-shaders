# NVIDIA render-scale tuning — 2026-08-29

## Outcome

The exact 33-transition NVIDIA matrix completed without interruption on
`WhiterunDragonsreach`. The overall verdict is **FAIL**: 24
transitions passed and 9 failed.

Eight vendor-native rows failed with
`physical_native_contract_mismatch` (all four DLAA rows and all four FSR
Native AA rows). FSR Quality row 16 also failed its strict deadline with
profile, active-operation, controller-settle, and relatch mismatches. Every
failed row nevertheless ended with a completed public operation, exact final
profiles, no active operation, no unresolved physical mutation, and clean
PID/build ownership, so the protocol continued safely.

All scaled DLSS rows passed. All scaled FSR3 rows passed except FSR Quality
row 16. Successful FSR rows resolved to `fsr_host` with no runtime fallback.
No measured DLSS trace recorded duplicated constants or evaluation failures.
None and TAA rows passed; exact native presentation generation was not exposed,
so that optional facet is retained as `INCONCLUSIVE: not_exposed` without
downgrading the core pass.

## Identity and fixture

- Build ID: `556d156f4afa08ae2105f2117929ccb5a41f9aca655b39f118a01b66a58574af`
- Source: `49761bc995f2f30c50b6ab812926abfc942e2110`
  (`v3.19.0-pr39-158-g49761bc99-dirty`)
- Adapter: NVIDIA GeForce RTX 4090, vendor ID `0x10DE`
- Skyrim VR PID: `35452`; DevBench: `1.15.4`
- Fixture: foveation `0.3`, periphery TAA center `0.3`, outer
  scale `0.7`; runtime-only and not persisted
- Matrix: schema 1, 33/33 rows, 5,000 ms pre-mutation pace, one strict
  waiter per row with a 30,000 ms upper bound

## NVIDIA DLSS and DLAA transitions

| # | Route | Result | Strict ms | Present ms | Cleanup ms | Backend / evidence |
| -: | --- | --- | ---: | ---: | ---: | --- |
| 3 | TAA → DLAA | FAIL | — | — | 2371.3 | physical_native_contract_mismatch |
| 4 | DLAA → DLSS Hoshipa | PASS | 2233.3 | 2233.3 | 2233.3 | dlss |
| 5 | DLSS Hoshipa → DLSS Ultra Quality | PASS | 3645.0 | 3645.0 | 3645.0 | dlss |
| 6 | DLSS Ultra Quality → DLSS Quality | PASS | 2622.9 | 2622.9 | 2622.9 | dlss |
| 7 | DLSS Quality → DLSS Balanced | PASS | 2445.4 | 2445.4 | 2445.4 | dlss |
| 8 | DLSS Balanced → DLSS Performance | PASS | 2371.0 | 2371.0 | 2371.0 | dlss |
| 9 | DLSS Performance → DLSS Ultra Performance | PASS | 2528.6 | 2528.6 | 2528.6 | dlss |
| 10 | DLSS Ultra Performance → DLAA | FAIL | — | — | 2591.2 | physical_native_contract_mismatch |
| 23 | None → DLAA | FAIL | — | — | 3490.9 | physical_native_contract_mismatch |
| 25 | FSR Native AA → DLSS Hoshipa | PASS | 4813.6 | 4813.6 | 4813.6 | dlss |
| 29 | FSR Ultra Performance → DLSS Ultra Performance | PASS | 4702.4 | 4702.4 | 4702.4 | dlss |
| 33 | None → DLAA | FAIL | — | — | 2560.0 | physical_native_contract_mismatch |

## NVIDIA FSR3 transitions

| # | Route | Result | Strict ms | Present ms | Cleanup ms | Backend / evidence |
| -: | --- | --- | ---: | ---: | ---: | --- |
| 13 | None → FSR Native AA | FAIL | — | — | 3025.5 | physical_native_contract_mismatch |
| 14 | FSR Native AA → FSR Hoshipa | PASS | 3886.3 | 3886.3 | 3886.3 | fsr_host |
| 15 | FSR Hoshipa → FSR Ultra Quality | PASS | 3176.6 | 3176.6 | 3176.6 | fsr_host |
| 16 | FSR Ultra Quality → FSR Quality | FAIL | — | — | — | profile_mismatch, api_operation_active, api_conditions_blocking, controller_not_settled, relatch_pending, api_active_contract_mismatch, physical_active_contract_mismatch |
| 17 | FSR Quality → FSR Balanced | PASS | 3372.4 | 3372.4 | 3372.4 | fsr_host |
| 18 | FSR Balanced → FSR Performance | PASS | 5688.6 | 5688.6 | 5688.6 | fsr_host |
| 19 | FSR Performance → FSR Ultra Performance | PASS | 3411.6 | 3411.6 | 3411.6 | fsr_host |
| 20 | FSR Ultra Performance → FSR Native AA | FAIL | — | — | 5208.7 | physical_native_contract_mismatch |
| 24 | DLAA → FSR Native AA | FAIL | — | — | 3450.0 | physical_native_contract_mismatch |
| 26 | DLSS Hoshipa → FSR Hoshipa | PASS | 4501.1 | 4501.1 | 4501.1 | fsr_host |
| 28 | None → FSR Ultra Performance | PASS | 4947.8 | 4947.8 | 4947.8 | fsr_host |
| 31 | TAA → FSR Native AA | FAIL | — | — | 4975.3 | physical_native_contract_mismatch |

## Provider and method crossings

| # | Route | Result | Strict ms | Present ms | Cleanup ms | Backend / evidence |
| -: | --- | --- | ---: | ---: | ---: | --- |
| 1 | DLSS Hoshipa → None | PASS | 2202.8 | 2202.8 | 2202.8 | native |
| 2 | None → TAA | PASS | 2131.0 | 2131.0 | 2131.0 | native |
| 3 | TAA → DLAA | FAIL | — | — | 2371.3 | physical_native_contract_mismatch |
| 11 | DLAA → TAA | PASS | 2425.5 | 2425.5 | 2425.5 | native |
| 12 | TAA → None | PASS | 2967.3 | 2967.3 | 2967.3 | native |
| 13 | None → FSR Native AA | FAIL | — | — | 3025.5 | physical_native_contract_mismatch |
| 21 | FSR Native AA → TAA | PASS | 4586.5 | 4586.5 | 4586.5 | native |
| 22 | TAA → None | PASS | 5987.3 | 5987.3 | 5987.3 | native |
| 23 | None → DLAA | FAIL | — | — | 3490.9 | physical_native_contract_mismatch |
| 24 | DLAA → FSR Native AA | FAIL | — | — | 3450.0 | physical_native_contract_mismatch |
| 25 | FSR Native AA → DLSS Hoshipa | PASS | 4813.6 | 4813.6 | 4813.6 | dlss |
| 26 | DLSS Hoshipa → FSR Hoshipa | PASS | 4501.1 | 4501.1 | 4501.1 | fsr_host |
| 27 | FSR Hoshipa → None | PASS | 4999.8 | 4999.8 | 4999.8 | native |
| 28 | None → FSR Ultra Performance | PASS | 4947.8 | 4947.8 | 4947.8 | fsr_host |
| 29 | FSR Ultra Performance → DLSS Ultra Performance | PASS | 4702.4 | 4702.4 | 4702.4 | dlss |
| 30 | DLSS Ultra Performance → TAA | PASS | 5899.8 | 5899.8 | 5899.8 | native |
| 31 | TAA → FSR Native AA | FAIL | — | — | 4975.3 | physical_native_contract_mismatch |
| 32 | FSR Native AA → None | PASS | 6535.4 | 6535.4 | 6535.4 | native |
| 33 | None → DLAA | FAIL | — | — | 2560.0 | physical_native_contract_mismatch |

## TAA and None crossings

| # | Route | Result | Strict ms | Present ms | Cleanup ms | Backend / evidence |
| -: | --- | --- | ---: | ---: | ---: | --- |
| 1 | vendor-to-None: DLSS Hoshipa → None | PASS | 2202.8 | 2202.8 | 2202.8 | native |
| 2 | None-to-TAA: None → TAA | PASS | 2131.0 | 2131.0 | 2131.0 | native |
| 3 | TAA-to-vendor: TAA → DLAA | FAIL | — | — | 2371.3 | physical_native_contract_mismatch |
| 11 | vendor-to-TAA: DLAA → TAA | PASS | 2425.5 | 2425.5 | 2425.5 | native |
| 12 | TAA-to-None: TAA → None | PASS | 2967.3 | 2967.3 | 2967.3 | native |
| 13 | None-to-vendor: None → FSR Native AA | FAIL | — | — | 3025.5 | physical_native_contract_mismatch |
| 21 | vendor-to-TAA: FSR Native AA → TAA | PASS | 4586.5 | 4586.5 | 4586.5 | native |
| 22 | TAA-to-None: TAA → None | PASS | 5987.3 | 5987.3 | 5987.3 | native |
| 23 | None-to-vendor: None → DLAA | FAIL | — | — | 3490.9 | physical_native_contract_mismatch |
| 27 | vendor-to-None: FSR Hoshipa → None | PASS | 4999.8 | 4999.8 | 4999.8 | native |
| 28 | None-to-vendor: None → FSR Ultra Performance | PASS | 4947.8 | 4947.8 | 4947.8 | fsr_host |
| 30 | vendor-to-TAA: DLSS Ultra Performance → TAA | PASS | 5899.8 | 5899.8 | 5899.8 | native |
| 31 | TAA-to-vendor: TAA → FSR Native AA | FAIL | — | — | 4975.3 | physical_native_contract_mismatch |
| 32 | vendor-to-None: FSR Native AA → None | PASS | 6535.4 | 6535.4 | 6535.4 | native |
| 33 | None-to-vendor: None → DLAA | FAIL | — | — | 2560.0 | physical_native_contract_mismatch |

## Final telemetry and cleanup

- Strict receipts: 33 terminal; 24 satisfied; 9
  failed.
- CPU session 1: 139202 frames; queue hold
  0.517 µs mean /
  154.3 µs max;
  queue wait 0.057
  µs mean / 48.0
  µs max.
- GPU capture: 139203 frames; periphery-TAA pixel ratio
  0.497119, avoided ratio
  0.502881.
- Profiler capture 1 completed 300/300 frames; final profiler state is disabled
  and not capturing. Lifetime profiler frames: 32692;
  slot refusals: 0.
- Texture lifetime session 1 stopped: 4616 created,
  4335 destroyed, 281 outstanding
  (6167.48 MiB estimated);
  zero dropped texture records and zero recording/attach failures.
- Presentation probe session 2 stopped with 301734 completed and zero
  dropped readbacks; 2 were in flight in the stop receipt and the final status
  drained them to 0.
- Process-private delta across the measured owner:
  3505.34 MiB;
  GPU usage delta: 438.83
  MiB; terminal memory pressure: `Normal`.
- Guarded cleanup stopped stress 2, texture 1, probe 2, CPU 1, and GPU start
  frame 32393. Final API active operation ID is zero, and PID/Build ID remained
  unchanged.

## Evidence

The durable aggregates are this report and ledger column
`49761bc995f2f30c50b6ab812926abfc942e2110__20260829T100038310Z`.
The original `build/devbench-evidence` directory, machine-readable summary,
transition CSV, and direct MCP receipts are intentionally local rather than
versioned. The ledger retains their byte counts and SHA-256 identities where
available. No runtime log was read for this result.
