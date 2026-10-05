# Live kernel batching continuity

The stationary Bannered Mare run on 5 October proved private execution in C,
but only for two frames. Descriptor metadata refresh then retired the
adapter. Initial conservative ROI shapes also permanently latched fallback;
A/B rejected their producer/clear/consumer dependency ABI. These are adapter
admission and continuity failures, not evidence of an NR rendering failure.

## Live producer and results

The physical DevBench VR DLL matched its adjacent manifest, AIO receipt
and all 21 private payload files. No enabled loose, Overwrite or unmanaged
provider superseded it.

-   Source: `52f8b9ca2e2cf04051564bf36c3000a2659318dc`, clean.
-   Build ID: `abce9d5acc5688af854fddadbb9486dd408520dc0505ccc0a0a039ada20c591f`.
-   DLL: 32,083,456 bytes, SHA-256
    `e222f83522d9a77df013d9ce7fcc5926a7518652d29e6f9b0890483e73eca549`.
-   Provider: 310.8.0, SHA-256
    `8270b350cd82de5ce89806872cdd6b6a9249b80836b91bbeb3573470744cc206`.
-   Adapter: NVIDIA RTX 5070 Ti Laptop, device `0x2f58`, SM120.
-   Skyrim PID 45484, started `2026-10-05T00:04:31.7436764Z`.

AI was disabled for stationary measurements. The unchanged native-eye view
showed three nearby characters. A/B evaluated at 1512 x 1680 per eye;
C ran before DLSS at 1008 x 1120. FOV was 0.95 during NR checks.
The explicit temporal probe was not registered and lifetime tracing was off.

C produced 632 private launches across two batched frames, after three
original warmups: 3,160 logical versus 2,528 physical launches in total.
The selected pairs were `[0,3]` and `[1,2]`; every region retained its own
native context. A late descriptor refresh then caused safe adapter
retirement and original evaluation. A/B did not execute private kernels.

Bounded 120-frame windows measured the following stereo outer NR pass:

| Pipeline | Single ROI GPU ms | Independent GPU ms | Batched selection GPU ms |
| -------- | ----------------: | -----------------: | -----------------------: |
| A        |          25.28823 |           22.04082 |                 19.99826 |
| B        |          24.08410 |           18.89588 | See preserved raw timers |
| C        |          10.88990 |           17.53597 |                 13.46830 |

The batched windows used original fallback. A/B after-window checks rejected
the failed candidate state; their raw completed profiler samples remain
preserved, not promoted to qualification. Pending GPU mask bounds changed
native shapes even with AI off. These windows do not establish a private
kernel gain, a production cost profile or a 5-6 ms bound.

The session ended with 91,568 successful NR evaluations/output commits and
zero NR failures, device removals or quarantines. Master off/on and a short
moving control completed. AI, FOV, NR mode and profiler state were restored;
no settings were saved. Both native frames for each A/B/C capture completed.
Skyrim exited by `qqq`; the final process check found no Skyrim process and
RootBuilder's active hook record was absent. MO2 remained responsive.

## Captured-input qualification

An immutable standalone replay producer built from clean source
`9bf407ce0fdee7a5f12f5806addd37f82cbcb4fe` has SHA-256
`616edf64c8da941c3b0ef463524858d32af7d0bd774054e8f5f18487ad9814b2`.
Fresh A/B/C inputs each ran forward and shared N2 lanes, with three warmups
and two steady samples. All six lanes completed and retired their native
owners; each shared lane issued 316 private calls per steady sample.
All 60 region output files matched the original bytes exactly, including
coordinates, formats, dimensions and row extents.

This comparison preserves independent model context; it is not an HMD
visual review or a sustained live qualification. Compilation overlapped some
replays and timing samples varied substantially, so their raw times are
retained as diagnostics. The offline A dependency ABI passed every checked
field. The precise live A/B mismatch was not captured: a read-only packet
inspection failed, and its errors are preserved rather than treated as data.

## Repair and safety

A descriptor refresh now validates the entire retained original prefix and
submits it in original order before forwarding the metadata API. Remaining
calls in that frame remain native. No private launch can precede promotion;
partial submission, invalid commands, wrong thread, stale epoch or a failed
native call fail closed. Packet/resource owners retain the existing fenced
lifetime. The adapter remains eligible on subsequent frames.

Unmatched launch shapes use an original frame and retry later. Graph-family
qualification is separate from shape qualification, so only an unsupported
family latches that rejection. Native rectangles are never padded or paired
with an incompatible region. Producer/clear/consumer ABI mismatches also
retain original ordering for that frame and report the exact region and
field; private resource ownership, nonaliasing, device, catalog and ABI checks
remain required before any private dispatch.

These changes apply only to the selected kernel backend. They add no readback,
GPU pass, synchronization or per-frame file capture. The existing command
validation and submission helpers are reused. SE/AE original paths and
unsupported adapters retain their existing fallback. Runtime GPU cost and
new live continuity still require the newly built DLL; no performance gain
is claimed for this repair.

## Validation and next live check

The nine standalone test groups pass. Kernel-chain tests cover 128,219 checks,
including all five descriptor APIs, partial and complete prefixes, exact
packet ordering before metadata forwarding, native failure before forwarding,
malformed or duplicated command streams, private-attempt rejection, all six
dependency mismatch fields and subsequent-frame eligibility. All 236 DLL controller, packaging and shader groups pass, as do preset
regression and exact generation checks. The new base's provider-availability
changes required refreshing the existing settings fingerprint and extracting
the availability helper into the resource-key test fixture. All three presets
retain exactly the same values and revision 8. The resource-key test also
checks that a missing NR provider preserves normal FOV/periphery TAA despite
a saved enabled master preference. Full provenance is retained with the AIO.

Next test the new AIO in the same stationary view: A/B/C must continue NR
without faults while batching counters identify actual private execution.
Record sustained matched-context timing windows separately from original
fallback. If A/B remain ineligible, use the new exact dependency reason to
identify the native ABI difference before extending the kernel contract.
No kernel guard should be waived to make a counter rise.

All raw evidence remains local under
`build/validation/nr-descriptor-live-20261005T001106Z`, including the live
receipts, original DLL verification, native input bundles, failed tooling
receipts, profiler histories, exact-output audit and shutdown proof.
The separate DevBench hash-case defect is tracked as
`AUTO-20261005-011324326-CBC9D1C2` and repaired in automation PR #17.
