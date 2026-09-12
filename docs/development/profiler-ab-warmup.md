# A/B profiler warm-up

The automatic A/B test starts with the current TEST configuration (B).
That first interval is a warm-up: its samples, outlier history, excluded
frame count, and duration do not contribute to reported results. Measured
time starts at the first switch to the saved USER configuration (A).
Subsequent B intervals are measured normally.

Both the settings panel and the floating overlay label the initial interval
`Variant B (TEST) warm-up`. Stopping during warm-up produces no results.
Restarting clears the previous measurements and starts a new warm-up.

The test manager owns interval start, switch, and stop. The frame handler
refreshes timing and collects samples before changing configuration or
drawing settings, independently of performance-overlay visibility. The
total uses the latest frame time, independent of the UI refresh interval;
per-shader rows retain the renderer's existing smoothed estimates. Drawing
the results cannot refresh timing, reset intervals, or add samples.
Clearing results is available after stopping, so an active test retains the
configuration snapshots needed for switching and restoring TEST settings.
The manager is the sole owner of those snapshots. Starting a new test also
invalidates the cached settings-difference display. Intervals are clamped
to the UI's 0–10 second range; zero stops testing and cannot start a run.

Outlier history belongs to each variant separately. A slower B therefore
does not get rejected against A's baseline. Missing, zero, non-finite, and
over-100ms total times are rejected from the first measured frame. Invalid
shader timings are rejected too, while a finite signed Other residual is
retained because it subtracts independently sampled timing estimates.

Comparison rows require samples from both variants. Sample coverage is
sufficient only when each variant has at least 100 accepted frames, ten
measured seconds, and 80% accepted frames. Marginal coverage requires at
least 30 frames, five seconds, and 80% acceptance in each variant. These
thresholds describe coverage, not a statistical significance test. The UI
shows each variant's counts and duration; unmeasured B is never treated as
a zero-cost winner. Mean and median use the shared statistics utilities,
including averaging both middle samples for even-sized medians.

This adapts [open-shaders PR #626](https://github.com/alandtse/open-shaders/pull/626)
to the shared SE, AE, and VR profiler. It does not change render-scale
transitions or the separate DevBench bounded-capture protocol.

Build `ab_test_aggregator_test` with controller tests enabled, then run:

```powershell
ctest --test-dir build/ALL -C Release -R '^ABTestAggregator$' --output-on-failure
```

The regression uses explicit monotonic timestamps to check warm-up
exclusion, measured A/B means and counts, duration, duplicate switches,
outlier-history isolation, stopping before A, repeated stop, reset,
A-first sessions, invalid samples, unequal variant costs, per-variant
coverage, signed residuals, and even-sized medians.
Runtime validation should also compare visible and hidden
performance-overlay runs and confirm that stopping restores TEST settings.
