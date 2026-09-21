> Historical report, privacy-filtered for this export. Statements about
> current process state and planned work describe the original session.
> See the bundle README for later findings and omitted source files.

# Frozen Skyrim: Ghidra and live-memory findings

The investigation identifies invalid virtual calls through destroyed shadow
lights in Light Limit Fix during COC 3. The evidence strongly supports a
use-after-destruction race between native light cleanup and geometry lighting
setup. The later persistent NVIDIA/D3D11 wait is independently verified;
this investigation does not establish the mechanism connecting the two.

## Exact failing code

The matching DLL's private symbols resolve CommunityShaders+0x484410 to
LightLimitFix::BSLightingShader_SetupGeometry_GeometrySetupConstantPointLights,
at its second IsShadowLight() call. Ghidra 12.1.2 independently decompiled
the captured function at runtime address 0x7ff886a84030 and disassembled:

    mov rax,[r13+38h]       ; render-pass sceneLights array
    mov rbx,[rax+rdi*8]     ; selected raw light
    test rbx,rbx
    je ...
    mov rax,[rbx]          ; presumed vtable
    mov rcx,rbx
    call qword ptr [rax+18h]

The second call is at 0x7ff886a84410; the first is at
0x7ff886a8431d. BSLight::IsShadowLight is virtual slot 3 (+0x18).
The source corresponds to src/Features/LightLimitFix.cpp:1675 and :1690.

Of 1,033 first-chance access violations on main thread 47468:

-   138 fault at the second call instruction, with RAX=0x188f1d36b00.
-   895 fault executing unmapped address 0x33509950, with
    RAX=0x18632c10f00. This is consistent with the same invalid slot lookup.
    The journal lacks the original RCX and return address, so the exact caller
    of the first execution fault cannot be independently reconstructed.

## Object identity and ordering

The journal observed both lights' construction and destruction. At the
preserved dump, both have zero reference counts and a descriptor pointer
in the first word where a live light requires its vtable.

| Known light generation | Light address | First word / descriptor | Descriptor qword +0x18 |
| ---------------------- | ------------- | ----------------------- | ---------------------- |
| 28                     | 0x18632c11600 | 0x18632c10f00           | 0x0000000033509950     |
| 37                     | 0x188f1d34800 | 0x188f1d36b00           | 0x33bbbd2e33c6e3e9     |

The first slot value exactly matches the observed execute-fault address.
The second slot value is not a canonical x64 code address; its descriptor
address matches RAX in the faults at the call instruction. These heap words
are later snapshot observations, not a recording of their original writes.

QPC timestamps establish the following order relative to the first exception:

| Event                                  | Thread | Time before exception |
| -------------------------------------- | ------ | --------------------- |
| Generation 28 descriptor render ends   | 47468  | 5,534.3 us            |
| Generation 28 light destruction begins | 9748   | 114.2 us              |
| Its descriptor destruction ends        | 9748   | 39.7 us               |
| Generation 28 light destruction ends   | 9748   | 25.8 us               |
| Generation 37 light destruction begins | 9748   | 23.9 us               |
| First execute access violation         | 47468  | 0 us                  |

The first exception occurs 21.4435 ms after the COC command. Sequence numbers
can interleave across threads; the table uses recorded QPC timestamps.

Ghidra also verified the native cleanup caller in SkyrimVR+0x12F77E0.
At +0x12F7C2E it atomically decrements [light+8], and at +0x12F7C3B it
calls the virtual release slot when the prior reference count was one.
The journal's destruction stack contains the exact return address
+0x12F7C3E, followed by the parabolic deleting destructor and the hooked
base-light destructor. This ties the observed destruction to a last-reference
release, rather than merely finding a possible cleanup routine.

## Why the process survives the faults

The lighting routine wraps its work in **try/**except and clears the strict
light data on an exception (current source line 1701). The matching binary's
handler clears offsets +0x80 and +0x88. This is consistent with repeated
handled faults and the main thread subsequently reaching presentation.

The journal decoder reported zero lifetime candidates because its automatic
checks cover the instrumented render scopes. Those scopes had already ended;
the unsafe use here is the later geometry-setup IsShadowLight lookup.
Zero candidates therefore does not rule out this use-after-destruction path.

## Present freeze and remaining uncertainty

A fresh noninvasive attachment found the same main-thread wait as the earlier
full dump: CommunityShaders compositor submit -> SteamVR vrclient_x64 ->
D3D11 Flush -> NVIDIA nvwgf2umx -> NtWaitForSingleObject. DevBench still reports
frame 64346. This establishes persistent presentation blockage, but does not
prove whether the invalid-light accesses caused the driver wait or whether
a related teardown problem caused both.

The actionable code target is lifetime-safe access to render-pass sceneLights
through geometry setup and native teardown. Additional null checks or the
existing exception handler do not establish object lifetime. Before changing
ownership, verify the applicable engine synchronization/retention contract.

## Provenance and validation

-   Exact PID 5236, started 2026-09-12T05:17:22.8630494Z.
-   Build d1b5a347cf75b29973f8e5a8d22477ce3e4a112b057566f329a7c218da4a7e6d.
-   DLL/PDB and manifest matched the preserved producer receipt.
-   Read 589,824 decrypted native code bytes from the frozen process.
    They match the same address range in the earlier full dump byte-for-byte.
-   Ghidra successfully imported and saved both native and DLL code programs;
    the selected decompilations reported completion.
-   Ghidra MCP was unavailable; its configured port 8080 belonged to MO2.
    Analysis used installed Ghidra headless with captured process bytes.
-   All CDB attachments were noninvasive and detached. Skyrim was not resumed,
    terminated, patched, or sent further gameplay commands.

Key evidence: ghidra-llf-analysis.txt, ghidra-release-callers.txt,
ghidra-shadow-analysis.txt, lifetime-correlation.json,
destroyed-light-memory.json, exception-site.log, live-inspect.log,
capture-native.log, and ghidra-input-identity.json. The saved Ghidra project
is projects/FrozenSkyrim5236.gpr. Raw bytes and scripts are retained.
