# Upstream main review after v1.8.4

Review upstream PRs chronologically against `main-VR`, implementing only
user-approved changes. Exclude E11-only changes, EHF, translations,
upstream-specific UI, and Skyrim 1.7.99 support. Inspect mixed PRs for
independent useful changes. Defer compilation and build-based tests until
the selected ports are complete, as requested by the user.

Pinned upstream range: v1.8.4 (`02646c3008dd7cae91fc790c67c0f342a29aa938`)
through main (`5db085e77951b84bd6c191ff5b06d56893f08286`). The review starts
from `main-VR` commit `b6c7b431d79dc213a213d6632c589389eeba555d`.

## #2673: frame-generation allocator synchronization

Approved selective port of upstream commit
`c4b294f1a9cd201a8f4ebc1d3216d573f13b433f` by Shaun Ren.

`main-VR` already advances interop fence values before signaling and
handles counter exhaustion. The missing protection was a CPU completion
wait before reusing a D3D12 command allocator. Track each allocator's last
successful queue signal and wait before resetting that allocator. Retain
the tracking across buffer resizes and clear it on resource teardown.
Record submissions even when presentation returns a retryable result.
Wait failures use the existing proxy quarantine path. The existing VR
frame-generation exclusion is unchanged.

Validation:

-   `pwsh ./tools/git.ps1 diff --check`: passed.
-   `pwsh ./tools/pre-commit.ps1 run --files src/Features/Upscaling/DX12SwapChain.cpp src/Features/Upscaling/DX12SwapChain.h`: passed.
-   `pwsh ./tools/generate-unified-presets.ps1 -Check`: all three tiers verified.
-   Release DLL and focused interop-test build: cancelled on the user's
    instruction to defer compilation. No build or runtime pass is claimed.

## #2674: Skyrim 1.7.99 support

Excluded by explicit user instruction. No part of this PR was ported.
