# SSS + TB + Depth Culling Analysis (Flatrim TB Test Mode)

## Context
- Test setup used:
  - `Terrain Blending` enabled
  - `Allow SSS with TB + Depth Culling` enabled
  - `Force VR TB depth path in flat` enabled
  - `Flat test: emulate VR depth culling` enabled
- Purpose: isolate whether ground shadow artifacts are caused by SSS itself or by TB/depth-culling depth routing.

## Observations (from test session)
1. With TB + forced VR-path test mode, ground shadows appear and are partly SSS-dependent.
2. Even with SSS effectively disabled, there are still TB + depth-culling dependent ground shadow patterns.
3. Disabling slot 2 override removes the uniform ground shadow on blending meshes; this matches prior VR behavior.
4. Disabling slot 17 override did not visibly change this specific shadow artifact.

## Code-path interpretation

### 1) The non-SSS base artifact is primarily in the TB/depth-culling path
- TB VR-path test mode depth behavior is controlled via:
  - `ShouldUseBlendedDepthSRV(...)` in `src/Features/TerrainBlending.cpp`
  - flat culling emulation in `IsVrPathDepthCullingEnabled(...)`
- This means a depth-source mismatch can exist before SSS runs.
- The fact that artifacts persist without SSS indicates SSS is not the root source.

### 2) SSS is amplifying an existing depth pattern, not creating it
- SSS enable is constraint-aware in `src/Features/ScreenSpaceShadows.cpp` (`Prepass()` checks `FeatureConstraints` and then calls `DrawShadows()`).
- When SSS is allowed, it ray-marches over the already-incorrect depth/shadow context and visually extends or emphasizes the pattern.
- So SSS-dependent shadows are secondary to the TB/depth-culling base issue.

### 3) Slot 2 is strongly implicated for this artifact class
- Slot 2 override is applied in TB hook paths:
  - `OnBeginTechnique(...)`
  - `OnUtilitySetupGeometry(...)`
  - `OnShaderPropertySetupGeometry(...)`
  - `OnSetDirtyStates(...)`
- Each path writes slot 2 through `ApplyPixelShaderSlotOverride(...)` when enabled.
- Your result that disabling slot 2 removes uniform ground shadows is strong evidence that the slot 2 depth binding is the dominant trigger for this specific artifact.

### 4) Slot 17 is likely not the primary source for these ground shadows
- Slot 17 is also overridden in TB hook paths, but disabling slot 17 did not affect this artifact in this scene.
- Slot 17 remains important for OBB/depth consumers and culling stability, but it does not appear to drive this ground shadow pattern.
- Also note: there is a global slot 17 bind path in `src/State.cpp` (`PSSetShaderResources(17, ...)`), so toggling TB hook slot 17 override may have reduced visible impact in some scenes.

## Practical conclusion
- Current evidence supports:
  - **Primary source:** TB + depth-culling depth interaction, especially via slot 2 override path.
  - **Secondary effect:** SSS magnifies a pre-existing shadow/depth pattern.
  - **Not primary in this case:** slot 17 override.

## Recommended short-term test policy
- Keep slot 2 override toggle available for diagnosis and regression checks.
- Treat slot 17 separately as a culling/OBB stability control (not a direct fix for this ground-shadow artifact).
- When evaluating SSS changes, first verify the base TB + depth-culling pattern with SSS disabled, then re-enable SSS to measure amplification.

