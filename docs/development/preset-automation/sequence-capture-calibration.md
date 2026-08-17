# Sequence capture calibration

Status: calibrated for the initial AMD native-OpenVR null-HMD lane

## Scope

This record establishes lossless sequence profiles for shader temporal and stereo review. It measures the capture pipeline, not shader performance. Timing passes must still be run separately with `communityshaders.profiler` and no screenshot session active.

The calibration was run on 17 August 2026 with:

- AMD Radeon RX 7900 XT;
- Skyrim VR using native OpenVR through SteamVR 2.16.7;
- SteamVR null HMD at 1512 x 1680 per eye, 90 Hz, 63 mm IPD;
- CSX VR Release/Info from `feat/preset-calibration-automation`;
- fixed RiftenCityNorth position `[172837.375, -93980.4453125, 11136.375]`, camera `[172837.375, -93980.4453125, 11169.5]`, yaw `1.6824611425`;
- SkyrimCloudyFF weather around game hour 7.4; and
- output on the local `D:` volume.

The BMP-capable build was commit `878796872`. Its DLL SHA-256 was `AEFAA7B8A5ABB585A5C969801A8ADD7006BB1F867723570ED4E55B9E087FBF92`.

## Results

An interval is measured in accepted OpenVR compositor cycles. At 90 Hz, intervals 3 and 4 target 30 and 22.5 samples per second respectively. A run is considered lossless only when all requested frames save successfully and both `backpressureDrops` and `incompleteStereoDrops` are zero.

| Format and output | Frames | Interval | Saved | Backpressure | Incomplete stereo | Token span | Result |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| PNG, separate eyes | 30 | 6 | 30 | 84 | 0 | 678 | Rejected for temporal sampling |
| PNG, separate eyes | 30 | 24 | 30 | 1 | 0 | 720 | Rejected: one drop |
| PNG, separate eyes | 60 | 30 | 60 | 0 | 0 | 1770 | Valid sparse stereo sequence |
| PNG, combined | 60 | 15 | 60 | 37 | 0 | 1440 | Rejected for temporal sampling; preview succeeded |
| BMP, combined | 10 | 1 | 10 | 15 | 0 | 24 | Rejected; bounded queue correctly skipped overloaded cycles |
| BMP, combined | 30 | 2 | 30 | 16 | 0 | 90 | Rejected; effective cadence fell toward interval 3 |
| BMP, combined | 30 | 3 | 30 | 0 | 0 | 87 | Valid dense sequence |
| BMP, combined plus 30 fps preview | 120 | 3 | 120 | 0 | 0 | 357 | Valid sustained dense sequence; preview succeeded |
| BMP, separate eyes | 60 | 3 | 60 | 7 | 0 | 198 | Rejected: file-pair overhead exceeded the envelope |
| BMP, separate eyes | 60 | 4 | 60 | 0 | 0 | 236 | Valid dense stereo sequence |

The combined BMP was 19.38 MiB per stereo frame. The sustained 120-frame run plus its MJPEG AVI occupied 2410.3 MiB. BMP therefore shifts the limiting resource from PNG compression CPU time toward storage bandwidth and capacity; free space must be checked before long campaigns.

Authoritative external artifacts are retained under:

`D:\Games\Skyrim\MadGod2\overwrite\Root\CSX Baselines\20260817-sequence-calibration`

and:

`D:\Games\Skyrim\MadGod2\overwrite\Root\CSX Baselines\20260817-sequence-calibration-bmp`

Each session directory contains its evolving/final `capture-manifest.json`. The zero-drop sustained combined run is `CS_Capture_2026-08-17_13-30-56_112_riften-120f-i03-combined-bmp-preview30_0004`; the zero-drop dense separate-eye run is `CS_Capture_2026-08-17_13-31-34_391_riften-60f-i04-separate-bmp_0006`.

## Adopted profiles

Use three evidence profiles rather than one compromise:

1. **Temporal dense:** combined BMP, interval 3, 30 fps preview when helpful. This is the default for flicker, history instability, disocclusion, and rapid view-dependent change.
2. **Stereo dense:** separate-eye BMP, interval 4. Use it for inter-eye mismatch, double vision, eye-dependent culling, and stereo temporal behavior.
3. **Archival sparse:** separate-eye PNG, interval 30. Use it when compact long-term retention matters more than temporal density.

The AVI remains a review derivative. Lossless frames and their exact compositor-cycle tokens are authoritative. Any run with a non-zero drop count is invalid for cadence-sensitive conclusions even when all requested output indices eventually save.

## Revalidation triggers

Repeat the short interval sweep when any of these change materially:

- render resolution or HMD profile;
- output volume, filesystem, or storage contention;
- screenshot worker count, readback implementation, image codec, or preview encoder;
- game/runtime lane, including physical HMD, OpenXR, OCU, or VDXR;
- CPU platform or sustained background CPU load; or
- a shader candidate changes frame time enough to alter capture-worker scheduling.
