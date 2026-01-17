# TB VR notes

This file captures the Terrain Blending (TB) VR analysis and proposed fixes so we can reference it later.

## Key premises for TB
- Terrain renders after all opaque objects.
- Terrain uses a translucent material (alpha blend).
- Terrain still writes to depth (unusual for translucent).

Why this is done:
- Avoid sorting issues.
- Softly blend terrain with objects.
- Let other translucent elements (particles) blend with the terrain depth.

## Pipeline summary
- Terrain passes are queued during normal rendering and drawn later during the blended decals stage.
- During the main camera depth-only prepass, TB renders terrain depth into a separate depth target.
- A compute pass min-blends main depth and terrain depth into a blended depth texture.
- During TB rendering, the shader samples the depth mask at t55 and uses a depth delta to compute alpha.

## Commit sequence (VR)
1) d7e5c57: TB trigger was tied to Main_RenderDepth. In VR this often corresponds to shadow or auxiliary depth,
   so TB only triggered when a shadow light was active. This broke the normal trigger logic.
2) 589cb91: Moved TB trigger to the main camera depth-only prepass (kMAIN DSV + no RTVs). This made TB trigger
   reliably, but it also redirected the engine depth SRVs to the blended depth for the rest of the frame, which
   caused head-locked shadows because shadow reconstruction reads the wrong depth.
3) 0d3e212 and 8fdc64f4: Added per-pass depth SRV override for TB rendering and a t17 binding change. These did not
   fix the shadow issue because the global depth SRV override was still active.
4) af60265: Added logging only.

## Trigger alternatives to 589cb91 (VR)
- Keep Main_RenderDepth as the trigger, but only accept the true main prepass (kMAIN DSV bound and no RTVs).
- Detect the true prepass by technique/renderFlags signature instead of DSV pointer matching.
- Gate the trigger on world-render boundaries and start TB on the first depth-only pass after world render begins.
- As a last resort, run an explicit TB prepass (render terrain depth into terrainDepth) each frame.

## Most likely fix (recommended)
- Keep the main prepass detection from 589cb91 (this is the correct trigger).
- Do NOT globally override the engine depth SRVs in VR.
- Instead, bind the blended depth SRV only during TB rendering (and optionally specific translucent passes that need
  soft blending). This preserves:
  - late terrain rendering
  - translucent blend
  - depth writes
  while keeping shadows correct because they still read the engine depth.

## Alternative fix
- Only override kPOST_ZPREPASS_COPY (not kMAIN) for soft blending, while keeping kMAIN intact for shadow
  reconstruction.
