# VR frame timing in Tracy

Build with `TRACY_SUPPORT=ON` for the native profiling zones.
`Game::MainUpdateCpu` measures elapsed time inside the engine's
`Main::Update` call on its thread. `Game::MainUpdateD3D11` is the
D3D11 timestamp span around commands submitted during that call. The existing
`FrameMark` records the game-frame interval, including time outside the
update. These three measurements describe different intervals; none is total
CPU busy time, physical HMD scanout time, or motion-to-photon latency.

On Skyrim VR, Tracy also plots each distinct OpenVR compositor frame's
pose-call-to-second-submit interval (`VR::PoseToSubmitMs`), pre- and
post-submit application GPU times, total-render GPU time, compositor CPU/GPU
times, and the client frame interval. The pose-to-submit interval may include
waiting and is not CPU busy time. OpenVR GPU timings can include work from
other processes. Present and dropped-frame counts and the compositor frame
index accompany the timings. A compositor-frame interval is plotted only
between consecutive indices; a skipped index records the observed gap and
index advance without inventing an intermediate frame time. Invalid or
missing timing samples are omitted.

The OpenVR plots are sampled only while Tracy is connected and appear at the
sampling call, which may lag the compositor frame they describe. Match the
trace window to fpsVR's recorded window when comparing player-visible cadence
with code-level zones. Treat fpsVR's CPU, GPU, and overall frame-time values as
separate observations; do not sum them or infer a whole-frame result by
summing Tracy pass zones. With a null HMD, neither source measures physical
headset scanout.
