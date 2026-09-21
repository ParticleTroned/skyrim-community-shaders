> Historical report, privacy-filtered for this export. Statements about
> current process state and planned work describe the original session.
> See the bundle README for later findings and omitted source files.

# PR #96 second adversarial review

Reviewed head `90d545008c1dda2b0d9e9d91b82021281698bbcd` and the complete
seven-file PR against `main-VR` at `5adb39d62981f855fd57af77c45bd57cf9b8a233`.

No additional production-code defect was identified within the documented
light-lifetime contract. The follow-up changes tests and documentation only.

## Gap corrected

The adapter execution test used one saved register and 0x70 bytes of local
stack space, while the native function saves three registers, reserves
0x40 bytes and stores RBX and the render index in caller home space. It also
returned directly after the adapter without exercising the continuation
jump. Argument/alignment checks alone did not exercise this native layout.

The test now uses the captured native prologue and register restores,
places its call and continuation at the native offsets, executes the
installer-generated adapter and verifies restoration of a saved state word.
Trap instructions occupy the displaced body to expose a bad return jump.
The existing installation assertions independently check branch targets.
This harness does not execute CommonLib's trampoline writer or register
the engine's exception-handler metadata; no such validation is claimed.

## Review coverage

-   Checked CommonLib's NiPointer copy/release behavior, derived-to-base
    ownership conversion and duplicate-key retention in the shared helper.
-   Rechecked capture failure, native index progress/overflow, stale-key
    rejection, pending-owner coverage and ownership independent of resets.
-   Inspected the actual SKSE five-byte branch writer and allocation path,
    and compared caller stack/return conventions with retained native code.
-   Rechecked admission before either write, native continuation restores,
    null runtime paths and localized VR 1.4.15 scope.
-   Existing snapshot, instruction matcher, queue-lock guard and trampoline
    remain shared. No new helper or runtime surface was introduced.

## Validation

-   Focused build passed. Both production-function tests passed in 0.15 s,
    with 744 hook assertions and 13 allocation-failure cases.
-   GitHub PR Checks and WIP completed successfully for reviewed head
    `90d545008`; newer commit status is recorded separately at publication.
-   The preceding clean universal DLL remains the production validation:
    Build ID `cd983f022fb67aa4ff83b251aa5b63adaa499b6d44ea568089f1f4d83719e4fe`,
    SHA-256 `f24eb30a25d204d94666288f6550e9f687b61bbc40267bb14f649be66129e298`.
    This test/documentation-only change does not rebuild or reidentify it.
-   Final scoped formatting, source-equivalence checks, current artifact
    hashes and publication readback are recorded in adjacent receipts.

No in-game test, native exception-handler qualification or performance
measurement was performed. The draft's existing limitations remain.
