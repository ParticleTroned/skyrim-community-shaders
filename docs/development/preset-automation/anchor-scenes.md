# Reproducible anchor scenes

Status: first `SVR-OVR-NULL` inventory validated on 2026-08-17

Machine-readable source: [`anchor-scenes.json`](./anchor-scenes.json)

## Purpose

These anchors give timing and visual campaigns repeatable coverage without relying on the direction in which a save happened to leave the physical headset. Each recipe establishes a cell, waits for streaming, applies time and (for controlled exterior variants) weather, then rejects the run unless live scene and camera readback match the manifest.

They are benchmark entry points, not complete scene determinism. NPCs, particles, foliage animation, exposure history, and other temporal state can still differ. Warm-up, paired-order balancing, and return-to-baseline checks remain mandatory.

## HMD control result

The SteamVR null HMD is active and produces exact accepted left/right OpenVR submissions at 1512 x 1680 per eye, 90 Hz. It is not yet arbitrarily steerable in this stack:

- DevBench `camera.freecam on` crashed Skyrim VR at `SkyrimVR.exe+08768CF`, with `devbench.dll+009D524` on the task call stack. The retained crash log is `C:\Users\Mark\Documents\My Games\Skyrim VR\SKSE\crash-2026-08-17-12-07-43.log`.
- `player.setangle z 0` and `player.setangle z 90` changed the actor request but did not change the submitted-eye camera. All three readbacks remained at yaw `1.682461142539978`, and the retained stereo captures kept the same framing.
- `coc` establishes stable entry poses for the validated anchors below, but a
  retained pose must still be rechecked from a fresh `coc`. The attractive
  Whiterun multi-depth proof failed that test: repeated entry produced position
  `[18245.435, -10745.442, -4616.525]` and yaw `2.2832105`, not the recorded
  proof position/yaw. Player position could be corrected, but submitted-view
  yaw could not. It is therefore a rejected candidate, not an anchor.

The next control proof should use a synthetic tracked-device/controller driver with an explicit SteamVR action binding, or a VR-safe correction to DevBench free-camera handling. SteamVR's input-binding debug option can help diagnose that work. Global overlay input is relevant only if an overlay emits actions; Arcade Mode is unrelated; Quick Calibrate changes the tracking origin and must not be used during a benchmark campaign.

## Validated inventory

| Anchor | Coverage | Entry and state | Expected camera yaw | Retained proof |
| --- | --- | --- | ---: | --- |
| `interior-dragonsreach-midday` | Bright/mixed interior; windows and volumetric rays; wood, cloth, metal, carved stone, deep hall | `coc WhiterunDragonsreach`; 12 s; `set gamehour to 12.0`; 1.5 s | `0.0167956073` | `CS_Capture_2026-08-17_12-13-59_571_dragonsreach-midday_0004` |
| `interior-bleak-falls-night` | Dark interior; firelight, rock relief, pottery/specular, deep shadow | `coc BleakFallsBarrow02`; 12 s; `set gamehour to 0.5`; 1.5 s | `-1.6507827044` | `CS_Capture_2026-08-17_12-20-15_971_bleak-falls-sanctum-night_0001` |
| `exterior-guardian-stones-clear-day` | Foliage, sky, distant mountains, stone relief, hard/soft shadow | `coc GuardianStones`; 12 s; 14:00; `fw 81A`; 2.5 s | `-1.5500009060` | `CS_Capture_2026-08-17_12-23-12_869_guardian-stones-clear-1400_0002` |
| `exterior-guardian-stones-storm-day` | Same pose under rain, overcast, wet/dark response and reduced visibility | Same entry; 14:00; `fw C8220`; 3 s | `-1.5500009060` | `CS_Capture_2026-08-17_12-23-55_104_guardian-stones-storm-rain-1400_0003` |
| `exterior-guardian-stones-clear-night` | Same pose under starlight and local emissive lighting; shadow/exposure floor | Same entry; 00:30; `fw 81A`; 3 s | `-1.5500009060` | `CS_Capture_2026-08-17_12-24-28_187_guardian-stones-clear-night-0030_0004` |
| `exterior-guardian-stones-fog-dawn` | Same pose under low-contrast depth fog and dawn exposure | Same entry; 07:00; `fw C821E`; 3 s | `-1.5500009060` | `CS_Capture_2026-08-17_12-24-50_131_guardian-stones-fog-dawn-0700_0005` |
| `exterior-windhelm-snow-midday` | High-albedo snow, dark carved stone, metal, firelight, foreground shadow | `coc WindhelmExterior01`; 12 s; `set gamehour to 12.0`; 1.5 s; retain natural `SkyrimCloudySN` | `-0.0450205393` | `CS_Capture_2026-08-17_12-25-29_941_windhelm-cloudy-snow-midday_0006` |
| `exterior-winterhold-seam-clear-day` | Diagnostic snow/stone contact edges with little foliage; close-wall framing limits general composition use | `coc WinterholdExterior01`; 12 s; freeze time; 14:00; `fw 81A`; 3 s | `-0.5312061906` | `CS_Capture_2026-08-17_20-35-56_801_Scout-WinterholdExterior01_0016` |

Proof captures are archived under `L:\CSX Preset Automation\Sessions\2026-08-17\MO2-overwrite\CSX Baselines\20260817-anchor-scout`. Every listed capture completed one raw `hmd_stereo` pair with separate eyes and combined derivative, zero failed frames, zero incomplete-pair drops, and zero backpressure drops.

The Winterhold proof was produced after the storage-policy change and is staged under campaign locator `anchor-scout/WinterholdExterior01-clear-1400`; the session finaliser moves it to the dated L: archive. It saved raw separate eyes without a redundant combined derivative. A second fresh `coc` reproduced its position and submitted-view yaw exactly.

The Whiterun multi-depth proof remains useful as rejected historical evidence at `L:\CSX Preset Automation\Sessions\2026-08-17\MO2-overwrite\CSX Baselines\20260817-distance-scout\CS_Capture_2026-08-17_16-16-34_026_Scout-WhiterunExterior01_0064`. Do not use it in a reproducible campaign until a save, synthetic HMD pose, or another validated recipe recreates its view.

The earlier Riften waterfront save remains useful for continuity with the first IBL campaign, but it is not the canonical entry for this new inventory. Its entry is `Save3_2FC025CB_0_776964646C65_RiftenWorld_000002_20260812061218_1_1`, cell `RiftenCityNorth` (`0x00042249`) in `RiftenWorld` (`0x00016BB4`).

## Accepted weather forms

| Editor ID | Form ID | Use |
| --- | --- | --- |
| `SkyrimClear` | `0x0000081A` | Clear day/night control |
| `SkyrimFog` | `0x000C821E` | Fog/low-contrast depth |
| `SkyrimOvercastRain` | `0x000C821F` | Available rain alternative; not yet an inventory anchor |
| `SkyrimStormRain` | `0x000C8220` | Storm/rain anchor |
| `SkyrimCloudyFF` | `0x0010A23F` | Existing Riften cloudy campaign |
| `SkyrimCloudySN` | `0x0010A245` | Natural Windhelm snow-region weather |

Use the console form without the leading zeroes, for example `fw C8220`. Do not infer success from command submission: wait, then verify the effective weather, time, cell, worldspace, player position, and camera through live readback.

## Execution contract

1. Start from a loaded, responsive game and confirm the intended runtime lane and Info shader-cache identity.
2. Submit the anchor's `coc`, wait 12 seconds, freeze timescale for a static visual comparison, set time, wait 1.5 seconds, then apply exterior weather and wait 2.5-3 seconds. Restore the declared prior timescale in cleanup.
3. Read `inspect scene` and `camera get`; reject any mismatch against [`anchor-scenes.json`](./anchor-scenes.json).
4. Warm the scene before timing. Keep capture disabled during timing.
5. Run a separate visual pass with exact `hmd_stereo` pairs and preserve the per-eye images and manifest.

Named-save creation through the current DevBench `game save` route did not produce a file in this VR session, so these first anchors intentionally use validated, self-checking recipes. Do not silently replace them with unverified saves.
