# FOV mask visualization

The VR FOV Mask Visualization setting previews the selected FOV-only or
FOV + TAA geometry, including a center scale of 1.00. Full coverage still
uses normal full-eye vendor upscaling when the preview is off. Previewing
does not activate cropped dispatch for other foveated features.

Mask drawing uses the existing periphery shader and presentation eye
outputs. It does not require vendor evaluation, encoded motion vectors,
depth guides, or a temporal input snapshot. Both main-pass VR upscaling
and render-scale submit presentation use the same mask draw. The normal
hidden-area cleanup, menu composition, and configured desktop mirror
remain in the presentation path. SE and AE do not enable the preview.

The setting is restored from saved VR profiles. Defaults remain off, and
non-VR configuration loading still clears VR-only settings. The existing
menu checkbox can be exercised through DevBench menu interaction in
builds with DevBench enabled; the renderer does not depend on DevBench.

## Protection and recovery

Native game menus, loading, pending post-load resets, invalid outputs,
and device loss retain their protection. The Community Shaders menu can
remain open while tuning the mask. Vendor retry backoff and ordinary-save
presentation delays do not suppress drawing with compatible resources.

Resource setup warms the preview shader before save protection begins.
During ordinary-save or lifecycle protection, preview drawing reuses
existing resources. If the shader was cleared or compatible outputs are
unavailable, it waits for safe resource preparation. Exceptions and failed
draws retain normal presentation. No resource-creation guard is released
by requesting a preview.

Previewing requests temporal history reset, clears reusable vendor-output
state, and does not count as a completed vendor frame or qualify the
six-frame ordinary-save recovery proof. Turning it off resumes normal
rendering through the existing admission and history-reset paths.

## Validation

The source-extracted `FoveatedMaskVisualization_off` and
`FoveatedMaskVisualization_on` tests exercise full coverage, both profiles
and eyes, missing resources, save/lifecycle reuse, loading/menu protection,
device loss, draw failure, and exception cleanup. `OrdinarySavePresentation`
also verifies that preview output revokes and cannot qualify save recovery.

Universal Release builds with SE, AE, and VR enabled passed with DevBench
both ON and OFF and Tracy OFF. All six focused CTest checks passed for
each build, including the source integration checks for profile loading,
output ownership, temporal-input bypass, and guarded mirror reuse.
Both DLLs passed their build-manifest hash and size verification. Build
logs, CTest XML, and preserved DLL receipts are under the local
`build/fov-mask-preview-review/` evidence directory. Changed-line C++ and
CMake formatting checks and the remaining scoped pre-commit hooks passed.

No in-game visual, save-timing, or performance measurements accompany this
implementation. Skyrim VR was not running during validation. The change
does not add measurement rows or claim render-scale qualification.
