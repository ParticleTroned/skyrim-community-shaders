# Full-resolution NR preparation review

The adversarial review of the port at `709eb9a1d` found that the late
draw-interface hook rebuilt a consumed full-resolution preparation. A
repeated or nested hook in the same frame could therefore evaluate NR again,
using a framebuffer already containing NR output or UI. The source branch
`d69bdb7eb` only consumed its prepared late state; the new full-resolution
fallback had bypassed that property.

Preparation now runs before scene post-processing for None/TAA and before
depth reconstruction for vendor upscalers. The late hook only consumes the
prepared state. A separate attempt-frame latch survives consumption,
settings changes and resource invalidation, so successful and failed
attempts both wait until the next frame before another capture. A new
attempt requires a fresh world source. Paused frames with a fresh world
remain eligible; retained framebuffers are not fed back through inference.

This applies to full-resolution NR, its FOV-only option and VR character
selection. Mono full-resolution support keeps the same early preparation
point. Existing rejection of mono character NR remains explicit.

Validation:

-   `pwsh ./tools/cmake.ps1 --build build/ALL --config RelWithDebInfo --target neural_full_resolution_preparation_test --parallel 2` passed.
-   `ctest --test-dir build/ALL -C RelWithDebInfo -R '^NeuralFullResolutionPreparation$' --output-on-failure` passed, 1/1.
-   The test compiles the production preparation function. It covers repeated
    and consumed frames; dimension, allocation and exception failures;
    same-frame disable, generation and size changes; next-frame recovery;
    retained-world rejection; paused fresh-world acceptance; and route gates.
-   Source assertions require early vendor/native preparation and prohibit
    preparation in the late draw-interface consumer.

These checks do not exercise NVIDIA inference or the game render hooks in
a live Skyrim session. Combined DLL validation is recorded by the parent
port review.
