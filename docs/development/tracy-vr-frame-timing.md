# VR frame timing in Tracy

Build with `TRACY_SUPPORT=ON` for the native profiling zones. With it off,
the DLL has no Tracy client linkage, main-update profiling hook, or compositor
history collector. The production performance menu retains its independent
single-frame OpenVR timing query. Texture-lifetime captures and structured
VR pipeline diagnostics, including their settings, require
`DEVBENCH_BRIDGE=ON`.

The broad main-update zones below are emitted only in VR.
`Game::MainUpdateCpu` measures elapsed time inside the engine's
`Main::Update` call on its thread. `Game::MainUpdateD3D11` is the
D3D11 timestamp span around commands submitted during that call. The existing
`FrameMark` records the game-frame interval, including time outside the
update. These three measurements describe different intervals; none is total
CPU busy time, physical HMD scanout time, or motion-to-photon latency.

On Skyrim VR, Tracy also plots recent OpenVR compositor entries'
pose-call-to-second-submit interval (`VR::PoseToSubmitMs`), pre- and
post-submit application GPU times, total-render GPU time, compositor CPU/GPU
times, and the client frame interval. `VR::PoseWaitElapsedMs` and
`VR::RenderToSubmitElapsedMs` split the pose-to-submit interval when all three
OpenVR offsets are valid. These elapsed intervals may include waiting and are
not CPU busy time. `VR::OpenVRCpuFrameMs` adds compositor-render CPU time to
render-to-submit time, following OpenVR's frame-timing example; it is not
whole-process CPU busy time. `VR::SubmitFrameCpuMs` is time spent in
`IVRCompositor::Submit`; `VR::PresentCallCpuMs` and
`VR::WaitForPresentCpuMs` expose presentation blocking and spin-waiting.
`VR::CompositorIdleCpuMs` reports compositor idle time, separate from
application CPU time. Direct CPU diagnostics and compositor CPU/GPU timings
retain OpenVR-reported zero values; nonfinite or negative timings are omitted.
OpenVR GPU timings can include work from other processes.

Present, mispresented, dropped-frame and reprojection fields, and the
compositor frame index accompany the timings. The collector reads two entries
behind the newest entry to reduce incomplete timing observations; presentation
counts still require care and are not a physical HMD delivery verdict. A
compositor-frame interval is plotted only between consecutive indices; a
skipped index records the observed gap and index advance without inventing
an intermediate frame time. Up to 16 unseen compositor entries are recovered
per poll; a larger gap remains visible in `VR::FrameIndexAdvance`. Invalid or
missing timing samples are omitted.

The OpenVR plots are sampled only while Tracy is connected and appear at the
sampling call. Recovered history entries may appear together there rather
than at their original compositor times. Match the trace window to fpsVR's
recorded window when comparing used application frames with code-level zones.
The fpsVR CSV has CPU and GPU timings and used-frame timestamps, but no
whole-frame-time column. Do not sum CPU and GPU values or Tracy pass zones to
infer a whole-frame result. With a null HMD, neither source measures physical
headset scanout.
