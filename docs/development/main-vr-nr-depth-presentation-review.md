# NR post-UI depth mapping review

The adversarial review found an inherited mismatch in the main late-NR
HMD-mask repair. After dynamic depth reconstruction, `kMAIN` contains
output-resolution stereo depth. The repair still passed the original
reduced input dimensions and eye offsets. This could sample the wrong
region, especially for the right eye, and clear visible output pixels.

The main repair now uses the output dimensions and output eye offsets.
It verifies the current depth texture and SRV identify the same resource,
and that its dimensions match the output layout. Because allocation size
alone cannot prove the content grid, scaled rendering also requires a
same-frame resource identity recorded after the actual depth reconstruction
draw. An early exit invalidates this proof. Native-sized depth needs no
reconstruction proof. Proof tracking runs only while VR NR is requested.
Submit-stage mappings retain their existing source-region contracts.

Validation:

-   `pwsh ./tools/cmake.ps1 --build build/ALL --config RelWithDebInfo --target neural_main_depth_presentation_test --parallel 2` passed.
-   `ctest --test-dir build/ALL -C RelWithDebInfo -R '^NeuralMainDepthPresentation(Contract)?$' --output-on-failure` passed, 2/2.
-   Policy checks cover native and scaled mappings, missing/stale/wrong-resource
    proof, invalid sizes and invalid frame identity. The production source
    contract checks invalidate-before-exit and stamp-after-draw ordering.
-   `pwsh ./tools/cmake.ps1 --build build/nr-color-tests --config Release --target nr_framebuffer_gpu_test --parallel 2` passed.
-   `ctest --test-dir build/nr-color-tests -C Release -R '^NRFramebufferWARP$' --output-on-failure` passed, 1/1.
-   The existing framebuffer WARP test now executes the optimized production
    `ClearHMDMaskCS` against asymmetric left/right hidden areas. Output-sized
    coordinates match every expected pixel; the reduced-coordinate control
    fails that reference, proving the case detects the reviewed defect.

No live headset or NVIDIA inference test ran for this focused correction.
