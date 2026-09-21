> Historical report, privacy-filtered for this export. Statements about
> current process state and planned work describe the original session.
> See the bundle README for later findings and omitted source files.

# Repeated render-batch crash, PID 54196

The original crash signature is confirmed again. This is an access violation,
not merely a hang: the apparent freeze was CrashLogger writing a full dump.
The game subsequently exited. The earlier PID 52916 hardware-breakpoint
exception is a separate, diagnostic-induced failure.

## Reproduction recovered from memory

The requested 25-transition assay issued two COCs. Transition 1, Windhelm to
Dragonsreach, reached strict completion in **5167.5335 ms / 25 frames**.
After the next 5000 ms wait, transition 2 issued `coc WindhelmExterior01` at
frame 20829. The saved scenario worker is waiting for transition 2's strict
completion; its expected destination is WindhelmExterior01. No terminal
receipt for transition 2 exists. The remaining 23 COCs were not issued.
Command-to-command spacing was 10263.1442 ms, including loading and the
five-second wait; this was not a fixed five-second dispatch period.

Nine completed scenario-step results were recovered from the dump using
matching PDB-local addresses and a bounded read-only decoder of the MSVC
release JSON layout. Container counts, ownership, Build ID, action names and
the pending request were cross-checked against PDB output and the preserved
original request. `recovered-scenario.json` preserves all 15,727 decoded JSON
values, including the complete first-transition receipt and retained status.
`summary.json` embeds these results and the deployment verification.

## Confirmed failure

-   Original exception: `0xC0000005`, thread 30792 / `0x7848`.
-   Instruction: `SkyrimVR+0x13480C8`, `mov qword ptr [rax+8],r13`.
-   `RAX=0x7FF6727923C8`, the runtime `bhkCollisionObject` vtable.
-   Invalid write destination: `0x7FF6727923D0`, value zero.
-   Accumulator `0x26EE72809C0` → batch at `+0x158` = `0x26E605BD6C0`.
-   Root batch group 1 at `+0x78` = `0x26E605CF040`.
-   Group's selected batch = `0x26E605CF070`.
-   Selected batch's list link at `+0x60` contains that physics vtable address.

The selected batch begins with zero; the parent accumulator and root batch
begin with heap pointers instead of valid class vtables. Several group
addresses and first pointers form a regular 0x30-spaced chain. Nearby storage
contains physics objects. This is consistent with released/reused storage
being followed during rendering. It does **not** identify which component
released the objects or prove an allocator free occurred at a particular
time. Full-dump memory is captured during crash handling, not as a history
of the preceding writes.

The call chain passes through Community Shaders' render annotation and
shadow-map wrappers. Their presence does not establish that they corrupted
the object. Physics vtable contents likewise do not identify a physics mod
as the culprit. Rebinding SKSE-task affinity across worker IDs is another
lead, not proof of a COC/render race. The saved scenario worker is waiting;
the other-thread snapshot did not identify an active corrupting writer.

## Evidence quality and retention

The archived dump has an ExceptionStream with the original fault context,
352 thread records, and 14,725 full-memory ranges. All 27,371,196,416 memory
payload bytes fit within the file. MemoryInfoList is absent; page protection
details must not be inferred from an unavailable memory-information stream.

Dump: `[REDACTED_LOCAL_PATH]`

Size: 27,373,316,616 bytes. SHA-256:
`83AC19156505A1A48DFA7181B12E030A326235B7CC6990F57B32E776F1E8EC98`.
There is one archived copy. Source and archive hashes were verified.

Crash capture began at 00:34:04.497 local and finished around 00:40:18.781.
The crash report's 00:40:18 header reflects report creation after dump writing.
The deployed DLL, producer Build ID, adjacent manifest and AIO receipt match:
Build ID `fb078514153553f30d89552b2d469ebc41a372a82407cbe817abf15ef3ac664a`,
compiled source `5655a7102d02d6ab40b14888a52596c4c9e9f450`.

Analysis used captured runtime instructions and memory, plus matching PDBs.
No static Ghidra analysis was used. No running Ghidra process or callable live
Ghidra tool was found in this session.

## Next diagnostic needed for a justified fix

The missing evidence is the lifetime history of the accumulator/batch chain.
Prepare a narrowly scoped in-process diagnostic that records creation,
destruction and ownership changes of the relevant shader accumulators and
batch renderers, with pointer, thread ID, frame, cell-transition generation
and a bounded native stack at release. Resolve and verify the relevant
runtime constructor/destructor entry points before installing any hook.
Capture this journal before invoking the original release operation.

Correlate the last release with subsequent render entry for the same pointer.
That distinguishes an engine teardown lifetime error, a stale owner retained
by a hook, and earlier memory corruption. Compare COC dispatch thread/phase
with normal console dispatch if the journal supports a scheduling race.
Do not treat an invalid-pointer early return as a correction to ownership.

Preserve the current CrashLogger capture configuration for the next run.
The hardware-breakpoint capture used for PID 52916 is withdrawn and must not
be reused. A debugger settings change alone does not repair this failure.

No runtime fix has been identified, applied, built or validated. The assay
failed overall; the first successful waiter does not make this a stability
or performance pass. This report preserves a diagnostic interruption and
does not publish a finalized performance comparison or ledger update.
