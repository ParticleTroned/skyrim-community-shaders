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
collects samples once per update, independently of performance-overlay
visibility. Drawing the results cannot reset intervals or add samples.
Clearing results is available after stopping, so an active test retains the
configuration snapshots needed for switching and restoring TEST settings.

This adapts [open-shaders PR #626](https://github.com/alandtse/open-shaders/pull/626)
to the shared SE, AE, and VR profiler. It does not change render-scale
transitions or the separate DevBench bounded-capture protocol.

Build `ab_test_aggregator_test` with controller tests enabled, then run:

```powershell
ctest --test-dir build/ALL -C Release -R '^ABTestAggregator$' --output-on-failure
```

The regression uses explicit monotonic timestamps to check warm-up
exclusion, measured A/B means and counts, duration, duplicate switches,
outlier-history isolation, stopping before A, repeated stop, reset, and
A-first sessions. Runtime validation should also compare visible and hidden
performance-overlay runs and confirm that stopping restores TEST settings.
