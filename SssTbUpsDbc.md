# SSS / TB / Upscaling / Depth Culling Notes

## Test baseline (current)
- TB enabled, depth culling enabled, DLSS upscaling on (not DLAA), SSS enabled.
- Shadowmask override remains wide (base/spot/pb/dpb).
- SSS depth source: kPOST_ZPREPASS_COPY.
- VR per-eye DLSS toggle exists for A/B tests; no visual difference observed.

## Findings
- Floating SSS shadows appear with upscaling (DLSS) but not with DLAA or when SSS is disabled.
- Disabling TB or depth culling does not remove the floating SSS shadows.
- Forcing `dynamicRes = {1,1}` removes floating shadows but caused a blocky transparent desync near the eye.
- Computing `dynamicRes` from the actual depth SRV size removes the big floating shadows and does not show the blocky desync.

## Root cause
- SSS was sampling depth with a dynamicRes scale that did not match the actual depth SRV resolution under upscaling.
- DLAA hides this because the scale is 1.0.

## Fix kept
- In `src/Features/ScreenSpaceShadows.cpp`, when upscaling is active, `dynamicRes` is computed from depth SRV size.
- VR: `dynamicRes.x = (viewportWidth * 2) / depthWidth`, `dynamicRes.y = viewportHeight / depthHeight`.
- Non-VR: `dynamicRes.x = viewportWidth / depthWidth`, `dynamicRes.y = viewportHeight / depthHeight`.
