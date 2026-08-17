# Capture and calibration evidence

CSX can capture bounded VR image sequences through the production screenshot path. The same facility is available from the Screenshot page and, when the Release+DevBench bridge is built and registered, from DevBench as `communityshaders.capture` at any log level.

The lossless PNG/BMP images and `capture-manifest.json` are the authoritative evidence. `preview.avi` is an optional MJPEG derivative generated only after image capture and encoding have finished. It is intended for quick motion review, not pixel comparison.

## CSX menu

In VR, open the Screenshot page and use **Short Sequence**. The controls select:

- frame count (1-240);
- interval in accepted OpenVR compositor cycles (1-120);
- preview playback rate (1-60 FPS);
- separate left/right output in addition to the combined image; and
- optional post-capture MJPEG AVI generation.

Short sequences require an HMD submitted-eye source. `HMD submission` records the submitted stereo pair, `Framed stereo` projects the pair into the existing 2560 x 1440 comparison view, and `Framed eye` records the selected eye only.

## DevBench API

Automated start and cancel operations are log-level-independent and do not require CSX Developer Mode. The opt-in boundary is the Release+DevBench build option together with a present DevBench host. Diagnostic texture readback and unrelated behavioural or test mutations remain Developer-Mode-only. Capability and status queries are read-only.

```json
{"action":"capabilities"}
```

The response advertises the access contract explicitly:

```json
{
  "action": "capabilities",
  "capabilities": {
    "automatedControl": {
      "available": true,
      "requiresDeveloperMode": false,
      "logLevelIndependent": true
    }
  }
}
```

```json
{
  "action": "start",
  "source": "framed_stereo",
  "label": "ssgi-balanced-a",
  "frameCount": 30,
  "frameInterval": 6,
  "previewFramesPerSecond": 15,
  "saveCombined": true,
  "saveSeparateEyes": true,
  "writePreviewVideo": true
}
```

```json
{"action":"status"}
```

```json
{"action":"cancel"}
```

Allowed sources are `hmd_stereo`, `framed_stereo`, and `framed_eye`. Numeric values outside the advertised limits are rejected rather than silently wrapped or clamped by the DevBench boundary.

Status and the manifest report every accepted frame index and compositor-cycle token, all output paths, encoding results, incomplete stereo-pair drops, and queue-backpressure drops. A left/right pair is accepted only when both submissions share the same accepted OpenVR compositor-cycle token. A new cycle discards an incomplete old pair.

CSX reserves only a small bounded number of screenshot readback/encoding slots. If the encoder cannot keep up, the session records backpressure and waits for a later eligible compositor cycle; it does not accumulate unbounded GPU or system memory. Consequently, wall-clock capture duration can be longer than `frameCount * frameInterval` under load.

## Calibration protocol

Do visual and timing runs separately:

1. Hold location, view, weather, time, and motion path constant.
2. Use `communityshaders.profiler` to enable profiling and collect the baseline/candidate timing pass without screenshot capture.
3. Repeat the same candidate with `communityshaders.capture` for visual evidence.
4. Retain the manifest with the images. Treat any backpressure or incomplete-pair count as evidence about the capture run, not as a missing game frame.
5. Compare lossless combined images for the ordinary view, left/right images for stereo-specific defects, and the AVI only for temporal inspection.

The profiler normally resolves GPU data several frames late. Compare `frame_count` with `capturedFrameCount`, prefer `resolvedTotalMs` for the nesting-correct GPU total, and use rolling `avgMs`, `p95Ms`, and `p99Ms` rather than a single current sample.

The first calibration target is AMD hardware. Preset conclusions must record the adapter/backend and must not be promoted to NVIDIA-specific conclusions until repeated on NVIDIA hardware. Capture and profiler tools deliberately do not rewrite arbitrary feature JSON: candidate setting changes continue to pass through each feature's production settings UI and validation path.

## Output contract

Each session creates a uniquely named directory beneath the configured screenshot path (or the explicit DevBench `outputPath`) containing:

- `capture-manifest.json`, atomically replaced as the session progresses;
- `frame_NNNNNN_stereo.png`/`.bmp` or the equivalent framed-eye image when combined output is enabled;
- `frame_NNNNNN_left.*` and `frame_NNNNNN_right.*` when separate eyes are enabled; and
- `preview.avi` when requested and successfully generated.

If preview generation fails, CSX removes the partial AVI and preserves the lossless frames. Preview failure is reported separately and does not turn an otherwise successful lossless capture into a failed capture session.
