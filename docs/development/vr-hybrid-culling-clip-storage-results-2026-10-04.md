# Hi-Z clip-storage comparison

Measured source `608aacfd9` at noon in save 22, WhiterunExterior01,
SkyrimClearTU, DLSS Quality K, 1344x1492 render pixels per eye. Player
position/weather/settings remained fixed. Observed HMD variation was
0.565 mm and 0.0624 degrees. Logging was unchanged; no images or traversal
diagnostic capture was required.

## Performance and rejection

Two 20-second windows per method had telemetry, traversal diagnostics and
profiling disabled. Means weight each window equally. Compilation was
inactive, source-compilation counters unchanged, and the compiler monitor
detected no build activity during accepted measurements.

| Mode             | CPU ms | GPU ms | Culling GPU ms | Rejected |
| ---------------- | -----: | -----: | -------------: | -------: |
| Advanced         | 10.411 |  7.840 |       0.081485 |  60.454% |
| Guarded 2x2 Hi-Z | 11.676 |  8.943 |       0.590532 |  40.539% |

Hi-Z costs an additional 1.2646 ms CPU (12.15%) and 1.1025 ms GPU (14.06%).
Advanced repeats were 10.598/7.912 and 10.225/7.769 ms CPU/GPU; Hi-Z
repeats were 11.444/8.785 and 11.908/9.100 ms. CPU and GPU overlap.

Culling uses separate complete 300-frame normal-shader captures, one per
method. Frozen counters span the capture and collection window, not only
the 300 frames. Hi-Z rejected 1,789,998 of 4,415,488 records across 1,078
batches. Advanced rejected 3,095,266 of 5,120,000 across 1,250 batches after
promoting 80,000 native-hidden records; before recovery it rejected
3,175,266. Both submitted 4,096 candidates per batch. The 19.915 percentage
point gap compares sequential cohorts, not matched unique objects or draw
calls. No Hybrid fallback, invalidated/unreadable history, resource
allocation or pipeline rebuild was recorded. Native batches were nonempty
and readable. Actual engine culling was enabled with extent 10.

| Hi-Z GPU scope   |       ms |
| ---------------- | -------: |
| Bounds testing   | 0.520383 |
| Base depth       | 0.023974 |
| Mip reduction    | 0.034659 |
| Hierarchy setup  | 0.000644 |
| Visibility setup | 0.005309 |
| Result copy      | 0.005563 |

Bounds testing is 88.12% of culling. Advanced downscale/bounds cost
0.030787/0.050698 ms. Hi-Z adds 0.5090 ms of producer GPU work. That is
about 46% of the whole-frame GPU difference, but the captures and frame
windows are separate: the remainder is not a measured extra-draw cost.
Fewer useful rejections could add engine drawing; matched outcomes/draw
work are needed to attribute it.

Hi-Z CPU prepare/dispatch/validation means were 3.637/24.618/13.313 us;
native staging readback was 3.516 us versus Advanced's 4.372 us. These
inclusive scopes must not be summed. Their small means do not establish
the cause of the 1.2646 ms whole-frame CPU difference.

## Next optimization

The indexed-face and alternating-clip implementation is active. Its
0.520 ms bounds measurement is below the preceding build's 0.724-0.751 ms,
but those runs used another process/view; this is not a controlled source
A/B proof. Rejection rates likewise cannot establish a quality improvement.

1. Safely skip clipping planes already containing a triangle. The current
   clipper still traverses four planes even when some cannot trim it.
   Retain conservative roundoff bounds and full clipping for uncertainty;
   preserve equality, interpolation, both-eye and capacity behavior.
2. Consider lazy reuse of triangle plane terms only if repeated work makes
   it worthwhile. Eager preparation of all twelve planes exceeds the
   earlier observed plane-attempt count by more than threefold and adds
   indexed storage. No hardware occupancy or spill measurement exists.
3. Address rejection separately with matched native/Hi-Z outcomes, then
   selective source-depth refinement at unresolved leaves. Farthest-depth
   2x2 reduction, expanded guarded regions and retained viewport/clip
   crossings remain conservative. Offscreen proxy count differences do
   not necessarily mean extra useful draws. Do not globally shrink guards
   or increase the read budget to chase the aggregate rate.

Halving bounds cost alone leaves about 0.330 ms Hi-Z culling versus
0.081 ms Advanced. Matching that native producer with unchanged overhead
would require about a 97.8% bounds reduction. Further targeted savings
are plausible; producer parity is not supported by this measurement.

The bundled controller retained raw results locally under
`build/astra-runtime/20261004T124751Z-hiz-clip-storage`. First-use setup
exceeded a five-second action deadline before any timing; subsequent
read-only observation verified the warmed backend. All four accepted
timing windows and both captures completed. Original Advanced, telemetry
and profiler/diagnostic settings were restored, with the scene at noon.
Motion/lifecycle and SE/AE runtime qualification remain open.
