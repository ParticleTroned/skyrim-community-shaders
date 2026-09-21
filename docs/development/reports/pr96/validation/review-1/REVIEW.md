> Historical report, privacy-filtered for this export. Statements about
> current process state and planned work describe the original session.
> See the bundle README for later findings and omitted source files.

# PR #96 adversarial review

Reviewed initial head `cddabac0c7563ed7886246470bd250de92e72030` against
`main-VR` at `5adb39d62981f855fd57af77c45bd57cf9b8a233`.

## Findings corrected

1. The adapter assumes RSI is the node and RSP+0x68 is the native index,
   but admission checked only the loop. A modified surrounding frame could
   pass that check and direct the replacement to the wrong stack location.
   Admission now checks all 184 bytes, covering setup, loop and restores,
   before allocation or either write. Every byte was compared with the
   preserved native capture; the test mutates each byte independently.
2. Empty, initially null-terminated and exhausted passes constructed a
   snapshot and retained every active/pending light despite having no
   dispatch. They now return while holding the scene lock, before any
   allocation or reference acquisition. Allocation injection verifies
   this property even with scene owners present and no memory available.

## Test gaps corrected

-   Record and verify both installed branch destinations, and execute the
    adapter bytes actually produced by the installer.
-   Remove all scene owners during the first render and verify later lights
    still render from the copied order before final destruction.
-   Drop the engine's last owner before throwing from rendering; verify
    snapshot destruction releases the final reference outside the lock and
    the rendering exception is not mistaken for capture allocation failure.
-   Exercise unsigned index wraparound from a nonzero starting index.

## Scope, correctness and DRY

-   No added diagnostics, settings, render-scale behavior, shader edits,
    D3D resources, feature dependencies or changes to SE/AE rendering.
-   Existing SceneLightSnapshot retains all five owning lists. The shared
    RetainScene helper preserves active order and defaults for LLF; native
    rendering suppresses the unused active enumeration. No parallel light
    lifetime cache or synchronization utility was introduced.
-   Raw accumulated entries remain lookup keys, never reference-acquisition
    sources. All retained owners outlive the whole native loop. Capture
    allocation failures release the scene lock before final ownership;
    virtual calls and normal/exceptional releases occur outside that lock.
-   Bounds, null termination, unknown/non-shadow objects, forward progress,
    and native index jumps were inspected. Existing virtual render hooks
    remain active. The tail adapter does not create an unregistered stack
    frame; it preserves the native return address. The harness verifies
    alignment and that return address, not engine exception-handler scopes.
-   The broader byte signature intentionally declines modified surrounding
    code instead of assuming compatible stack/register semantics. It runs
    only at installation and adds no per-frame signature scanning.

## Validation and limits

Both focused production-function tests passed in 0.16 seconds, including
13 allocation-failure cases and 743 hook assertions. Scoped formatting
passed. Final clean-source build and publication identities are recorded
in `validation.json` and `pr-publication.json` beside this review.

No in-game run or performance measurement was performed. PR #93's 70 COCs
used a DLL without this native fix and do not qualify PR #96. Snapshot cost
on nonempty passes remains unmeasured. Light-field mutations, unrelated
engine objects and the complete driver-corruption chain are outside this
lifetime contract. There are no remaining actionable findings from this
source review within that contract; runtime qualification remains pending.
