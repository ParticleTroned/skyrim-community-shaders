# SSS / TB / Upscaling / Depth Culling Notes

## Test baseline (current)
- TB enabled, depth culling enabled, DLSS upscaling on (not DLAA), SSS enabled.
- Shadowmask override remains wide (base/spot/pb/dpb).
- SSS depth source: kPOST_ZPREPASS_COPY.
- VR per-eye DLSS toggle exists for A/B tests; no visual difference observed.

## Findings
- Floating SSS shadows appear with upscaling (DLSS) but not with DLAA or when SSS is disabled.
- Disabling TB or depth culling does not remove the floating SSS shadows.
- With DLAA active, SSS shadows are now visible (forced combined SSS path for DLAA).
- With DLAA or upscaling active, the remaining moving ground shadows go away if TB is disabled or depth culling is disabled.
- Forcing `dynamicRes = {1,1}` removes floating shadows but caused a blocky transparent desync near the eye.
- Computing `dynamicRes` from the actual depth SRV size removes the big floating shadows and does not show the blocky desync.

## Root cause
- SSS was sampling depth with a dynamicRes scale that did not match the actual depth SRV resolution under upscaling.
- DLAA hides this because the scale is 1.0.

## Fix kept
- In `src/Features/ScreenSpaceShadows.cpp`, `dynamicRes` is computed from depth SRV size (not runtime ratios).
- VR combined: `dynamicRes.x = (viewportWidth * 2) / depthWidth`, `dynamicRes.y = viewportHeight / depthHeight`.
- VR per-eye: `dynamicRes.x = viewportWidth / eyeDepthWidth`, `dynamicRes.y = viewportHeight / eyeDepthHeight`.
- Non-VR: `dynamicRes.x = viewportWidth / depthWidth`, `dynamicRes.y = viewportHeight / depthHeight`.
