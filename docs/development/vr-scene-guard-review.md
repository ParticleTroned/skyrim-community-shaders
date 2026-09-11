# VR scene and shadow guard review

Reviewed on 2026-09-11 against the live-mapped Skyrim VR 1.4.15 image,
the actual generated guard code, and the installed Engine Fixes 7.7.1
configuration and startup log.

## Findings and repair

The shadow-camera guard's epilogue fingerprint contained `45` instead of
`41` at RVAs `134C9B2` and `134C9B7`. Those REX prefixes incorrectly
identified XMM14/XMM15 restores; the native epilogue restores XMM6/XMM7.
Commit `b829654ea23dc758c8d2c60af8aebc27b0533055` introduced the two
incorrect bytes. The mismatch prevented both camera guards from installing.
The fingerprint now matches the captured instructions exactly.

The scene guard required an unchanged interior virtual call at `CBFD24`,
although it patches only the five-byte prologue at `CBFC60` and returns
before establishing the native stack frame. Engine Fixes 7.7.1 installs a
five-byte branch at that interior call during preload. Its
[culling-freed-object implementation](https://github.com/alandtse/EngineFixesSkyrim64/blob/v7.7.1/src/fixes/culling_freed_object_crash.h)
and this session's log establish the overlap: all eight Engine Fixes
culling sites installed before the LLF instruction checks ran.

The scene guard now checks the complete 23-byte entry sequence that defines
its displaced instruction and incoming-object contract. It does not check
or overwrite unrelated interior instructions. This preserves the Engine
Fixes late virtual-call protection while allowing LLF to reject a null
object or cleared vtable before the helper's first object-field read.
A changed prologue still prevents installation; no detour chaining,
wildcards, module-name exemptions, or signature scanning were added.

## Scope, correctness, and shared implementation

-   Both installers remain restricted to VR 1.4.15. SE, AE, and unsupported
    VR versions install nothing through these paths.
-   The scene guard reproduces `mov [rsp+10h],rbx` and resumes at `CBFC65`.
    Invalid inputs return before any native pushes. The separate helper's
    tail jump at `CBFDBF` targets the same guarded entry. Other entry points
    and lifetime changes after validation are outside this guard's scope.
-   The camera entry guard validates the descriptor, accepted camera vtable,
    and frustum pointer before reproducing the six-byte entry sequence.
    The late guard separately checks the camera reloaded into RDI. Its
    invalid path removes the injected call's return address and jumps to
    the validated native epilogue, preserving the native frame teardown.
-   All camera instruction and vtable checks finish before either hook is
    written. Conflicting entry, late-use, or epilogue instructions reject
    installation. The complete native epilogue remains authenticated.
-   The late-load fingerprint includes the following `movups` instruction;
    only the original seven-byte pointer load is replaced. The preceding
    context is checked once, with no overlapping byte definitions.
-   All LLF instruction checks reuse `MatchesInstructions`. It now reuses
    the existing `IsReadableRange` helper and logs the RVA, byte offset,
    expected byte, and observed byte on mismatch. Readability and mismatch
    work occurs during installation, outside the render loop and COC assay.
-   The shared `VRValidatedObjectGuard` and existing camera-vtable list are
    unchanged. Plausible address checks are bounded defenses against known
    null/cleared/reused-object shapes, not proof of allocation ownership or
    synchronization against concurrent teardown. No new vtable is justified
    by this evidence.

## Independent instruction fixture

The compact fixture in `tests/fixtures/vr_scene_guard_sites.h` was extracted
from the saved live image, independently of the source fingerprints:

```text
C:/src/skyrim-vr-upscaler-standalone/live-ghidra-snapshots/20260822T111246Z/SkyrimVR-live-pid-39940-base-00007FF6F2B30000.bin
SHA-256: 1a6fbb7e726491929ea6eaa1dbfd866c48404edaefd63bd712f41fb13ffa2ce8
Captured image base: 0x7FF6F2B30000
Size: 60,133,376 bytes
```

| RVA     | Bytes | SHA-256                                                          |
| ------- | ----: | ---------------------------------------------------------------- |
| CBFC60  |    23 | ac1fad647749a022618abb12c0d1fefab79c051e536d8a2a017f5668bea63f72 |
| CBFD15  |    21 | be9cc48151dfce6b56e253f5a7371c2111c8d6f11514e0a155dc9c4f12652cae |
| CBFD52  |    22 | 30b33abeb69c98698be12f5b2d250d401a7f7d010356a8cf3a77b6de2482c48e |
| CBFDB2  |    18 | a47f837e8705c395a8ce1591770b1badedd1d945787db562a8b60f71674057a1 |
| 134C370 |    26 | 6f4d8a46e12b7a1d4c150ea99ad0d86ea744b541e9ddad177f1c0637296dd514 |
| 134C38A |    59 | cf64f197669b7e4e3594d9d1a945415cafe8b8cc6e9b28f2461fac90e1d0657c |
| 134C5F9 |    36 | 570a88690395036811ffc2a82624bfa566109bb5213add9b69e84bccb8e35241 |
| 134C99E |    68 | 15f60cbd519021b2e72cdb49690d99de2a807a5cc1a0684abb5692991cfaf381 |

The fixture includes the earlier body checks so compatibility regressions
can be reproduced without Skyrim or the full snapshot. It does not contain
the current crashed process's heap or prove its exact modified code bytes.

## Validation

The `VRSceneGuards` controller test compiles the actual installer and Xbyak
definitions extracted from `LightLimitFix.cpp`, using a synthetic mapped
image and a recording trampoline. It checks successful installation,
preservation of the Engine Fixes interior branch, rejection of each mutated
byte in every protected instruction range, unreadable memory, invalid
camera vtables, and unsupported runtimes. It also executes the generated
entry and late-use guards against controlled valid and invalid objects.
The late-use test executes the captured native stack/register setup and
epilogue, verifying RSP, all eight nonvolatile general registers, and
XMM6-XMM15 on valid and rejected paths for both accepted camera types.
All 364 assertions passed.

```powershell
pwsh ./tools/cmake.ps1 --build build/pbr-registry-fix --config Release --target vr_scene_guards_test -- /m:2 /v:minimal
& build/pbr-registry-fix/Release/vr_scene_guards_test.exe
```

The production translation unit was compiled using the existing universal
Release project, with SE, AE, and VR enabled; zero warnings and errors:

```powershell
& 'C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/amd64/MSBuild.exe' build/pbr-registry-fix/CommunityShaders.vcxproj /t:ClCompile /p:Configuration=Release /p:Platform=x64 '/p:SelectedFiles=C:\src\skyrim-community-shaders\src\Features\LightLimitFix.cpp' /p:BuildProjectReferences=false /m:2 /v:normal /nologo
```

All five repaired signature ranges matched the hash-verified live image.
The focused pre-commit checks cover only the changed files. Gersemi was
checked separately with `--line-ranges` for the two changed root CMake
regions and normally for the new extraction script; unrelated existing
root formatting was preserved. Its custom-command warnings remain visible.
The configure step retains the existing FidelityFX CMP0116 deprecation warning.

No DLL was linked or deployed, and no new game session or COC assay was
run for this commit. Engine Fixes coexistence is tested with its documented
interior branch footprint, not a new live session. The separate
`SkyrimVR.exe+1349238` render-pass-array CTD remains under investigation.
Local build, decode, and crash evidence is preserved in
`artifacts/simple-coc-25-5s-20260911T181040Z/`.

## Adversarial follow-up

The follow-up review found a validation gap: a simplified test epilogue
checked stack balance but could not verify the actual XMM6/XMM7 restore
instructions. It was replaced with execution of the captured native frame
setup and epilogue. The outer test harness saves its caller's nonvolatile
registers, seeds distinct values, and verifies every restored value after
the guarded function returns. Reintroducing the original two incorrect
REX prefixes into the test-owned executable buffer demonstrably leaves
XMM6/XMM7 unrestored and overwrites XMM14/XMM15. The captured fixture itself
is immutable during that check.

No additional production defect was found. The review reconfirmed the
23-byte scene-entry contract, independent interior-hook ownership, complete
camera epilogue authentication, rejection before any writes, and reuse of
the existing readability and object-validation helpers. Native decoding
also confirms that the late guard preserves the load's RAX result; R10 and
modified flags are defined again before native use. CommonLib's
boundary checks remain enabled: the entry patches end at instruction
boundaries, and the late patch replaces one seven-byte instruction with
the existing call plus two NOP bytes.

The test still uses a recording trampoline for installation and controlled
objects for execution. It does not establish live hook coexistence, object
lifetime synchronization, or a fix for the separate render-batch CTD.
