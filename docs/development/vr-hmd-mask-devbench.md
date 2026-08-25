# VR HMD-mask measurement

The VR hidden-area mask clear uses one production implementation:
`tiled_exact_5x5`. It preserves the exact thresholded radius-two 5x5
predicate while sharing depth reads across each 8x8 color tile. There is no
runtime selector, in-game toggle, alternate kernel, or algorithm fallback.

The shader supports equal-size and upscaling color mappings. The host rejects
downscaling mappings and retains the original compositor path instead of
changing the clearing predicate.

## DevBench contract

The `communityshaders.renderscale` tool retains two fixed-mode actions:

-   `ham_status` reports implementation, timer names, dispatch counters, and
    rejected dispatches.
-   `ham_reset` clears those counters before a bounded capture.

Include the exact `expectedBuildId` returned by discovery or status in each
call. HAM API version 3 reports diagnostic schema version 5. The stable GPU
timers are:

-   `Upscaling::HAM::InputTiledExact5x5`
-   `Upscaling::HAM::FinalTiledExact5x5`

Sum the active input and final timer for each resolved profiler frame when the
tested route uses both phases. A valid run keeps scene, render scale, quality
profile, frame cap, headset resolution, and thermal conditions unchanged.

## Controlled comparison

Save 14 was measured in the same loaded scene with 240 resolved profiler
samples per implementation, split across two ABBA legs. The values below are
combined per-frame HAM GPU time.

| Measured implementation | Mean (ms) | Median (ms) | p95 (ms) | p99 (ms) | 90 Hz budget |
| ----------------------- | --------: | ----------: | -------: | -------: | -----------: |
| Tiled exact 5x5         |    0.2564 |      0.2376 |   0.5315 |   0.6001 |        2.31% |
| Sparse depth 9-tap      |    0.2774 |      0.2611 |   0.2693 |   0.6502 |        2.50% |
| Exact reusable mask     |    0.3248 |      0.2949 |   0.6093 |   0.7229 |        2.92% |
| Untiled exact 5x5       |    0.4632 |      0.4301 |   0.7557 |   0.8540 |        4.17% |

Tiled exact reduced mean HAM cost by 7.58% versus sparse 9-tap, 21.07%
versus the reusable mask, and 44.65% versus untiled exact 5x5. All 960 timing
samples were preserved with zero profiler-slot refusals. Two DevBench
reconnects lost no samples.

A separate tiled-exact fidelity audit evaluated 811,478,400 display-domain
decisions and found zero mismatches, zero false negatives, and zero false
positives against the exact radius-two 5x5 reference.

## Conclusion

Tiled exact 5x5 is both the fastest measured implementation and exact in the
bounded fidelity capture. It is therefore the sole production path. The
comparison toggles, alternate shaders, reusable-mask resources, and fidelity
audit machinery were removed after selection; DevBench retains only the
fixed-path timers and counters needed for regression testing.
