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

Automated start/cancel and strictly allowlisted feature measurements are log-level-independent and do not require CSX Developer Mode. The opt-in boundary is the Release+DevBench build option together with a present DevBench host. Arbitrary diagnostic texture readback and unrelated behavioural or test mutations remain Developer-Mode-only. Capability and status queries are read-only.

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
  "format": "png",
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

Allowed sources are `hmd_stereo`, `framed_stereo`, and `framed_eye`. Allowed lossless formats are `png` and `bmp`; PNG is the compact archival default, while BMP avoids PNG compression CPU cost at the expense of much larger files. Numeric values outside the advertised limits and unknown formats are rejected rather than silently wrapped or clamped by the DevBench boundary.

### Named feature measurement

`screen_space_shadows_factor` is the first allowlisted feature measurement. It
captures the exact packed-stereo `R8_UNORM` factor buffer produced by the SSS
prepass. `1.0` means unshadowed; lower values are the factor later multiplied
into lighting. This makes sample/range attribution independent of exposure,
post-processing, HUD text, and most unrelated scene changes.

```json
{
  "action": "measure",
  "source": "screen_space_shadows_factor",
  "outputPath": "D:\\CSX Evidence\\state-10-factor-001.png"
}
```

Poll `{"action":"measurementStatus"}` until `state` is `complete`. Completion
requires both the lossless PNG and its `.png.stats.json` sidecar. Only one named
measurement may be pending or queued at once, an existing output is rejected,
and the request uses the screenshot worker's bounded diagnostic queue. The API
does not expose arbitrary GPU resources. The current factor target packs the
left and right eyes side-by-side; split it at half width for per-eye analysis.

`tools/preset-calibration-visual-sss-parameter-factorial.ps1` records a small
factor-frame set for every factorial state. Analyze it with
`tools/analyze-sss-factor-measurements.py`; red heatmap pixels mean the candidate
added shadow, while cyan pixels mean it removed shadow. Use
`tools/analyze-sss-factor-consensus.py` on reversed-order pairs to retain
same-sign changes and quantify the order residual against a negative-control
scene. Heatmaps auto-scale independently; compare their JSON magnitudes rather
than relative display brightness.

The first live Info-level proof used VR Release+DevBench DLL SHA-256
`26B6110286FB5A088BF0DEDABADC4AE12028CDB466E39E296833081C41ECE9B5`
at the canonical Guardian Stones entry. Capabilities advertised the named source
without Developer Mode; an unknown source and an existing output path were both
rejected. Measurement id 1 completed with a 3024 × 1680 factor PNG (1512 × 1680
per eye) and statistics covering 5,080,320 finite pixels, zero non-finite pixels,
and mean factor `0.96475648`:

- PNG SHA-256: `748992673708358752AEB012326EC478B5DCD1416E6C56F50D94E3DED89A3E30`;
- statistics SHA-256: `2F1E27BEC91972B76D4CAF1277CC9DEC5D210AF06B52AD0D5B1657FB33A7E659`;
- archived root: `L:\CSX Preset Automation\Sessions\2026-08-17\MO2-overwrite\CSX Baselines\preset-automation-sss-factorial\20260817-factor-api-proof`.

Status and the manifest report every accepted frame index and compositor-cycle token, all output paths, encoding results, incomplete stereo-pair drops, and queue-backpressure drops. A left/right pair is accepted only when both submissions share the same accepted OpenVR compositor-cycle token. A new cycle discards an incomplete old pair.

CSX reserves only a small bounded number of screenshot readback/encoding slots. If the encoder cannot keep up, the session records backpressure and waits for a later eligible compositor cycle; it does not accumulate unbounded GPU or system memory. Consequently, wall-clock capture duration can be longer than `frameCount * frameInterval` under load.

## Calibration protocol

Do visual and timing runs separately:

1. Hold location, view, weather, time, and motion path constant.
2. Use `communityshaders.profiler` to enable profiling and collect the baseline/candidate timing pass without screenshot capture.
3. Repeat the same candidate with `communityshaders.capture` for visual evidence.
4. Retain the manifest with the images. Treat any backpressure or incomplete-pair count as evidence about the capture run, not as a missing game frame.
5. Compare lossless combined images for the ordinary view, left/right images for stereo-specific defects, and the AVI only for temporal inspection.

Calibrate the format and interval on the actual output volume before a campaign. A zero-drop envelope measured on the current AMD/SteamVR-null lane is recorded in [sequence capture calibration](preset-automation/sequence-capture-calibration.md); it is a lane-specific starting point, not a portable throughput guarantee.

The profiler normally resolves GPU data several frames late. Compare `frame_count` with `capturedFrameCount`, prefer `resolvedTotalMs` for the nesting-correct GPU total, and use rolling `avgMs`, `p95Ms`, and `p99Ms` rather than a single current sample.

The first calibration target is AMD hardware. Preset conclusions must record the adapter/backend and must not be promoted to NVIDIA-specific conclusions until repeated on NVIDIA hardware. Capture and profiler tools deliberately do not rewrite arbitrary feature JSON: candidate setting changes continue to pass through each feature's production settings UI and validation path.

## Output contract

Each session creates a uniquely named directory beneath the configured screenshot path (or the explicit DevBench `outputPath`) containing:

- `capture-manifest.json`, atomically replaced as the session progresses;
- `frame_NNNNNN_stereo.png`/`.bmp` or the equivalent framed-eye image when combined output is enabled;
- `frame_NNNNNN_left.*` and `frame_NNNNNN_right.*` when separate eyes are enabled; and
- `preview.avi` when requested and successfully generated.

If preview generation fails, CSX removes the partial AVI and preserves the lossless frames. Preview failure is reported separately and does not turn an otherwise successful lossless capture into a failed capture session.
