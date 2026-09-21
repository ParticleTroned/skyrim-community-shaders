> Historical report, privacy-filtered for this export. Statements about
> current process state and planned work describe the original session.
> See the bundle README for later findings and omitted source files.

# Windhelm / Dragonsreach crash with WinDbg attached

The test reproduced a write access violation at **SkyrimVR.exe+0x13480C8**.
The recovered Windows crash dump establishes the immediate failure.
The code that introduced the invalid pointer is still unknown; no runtime fix
has been made or validated.

## Confirmed fault

-   Process 42300; fault thread 19368 (0x4BA8).
-   Exception 0xC0000005, write to 0x00007FF6727923D0.
-   Instruction at 0x00007FF6724F80C8: `mov qword ptr [rax+8],r13`.
-   RAX = 0x00007FF6727923C8; R13 = 0.
-   RBP = 0x00000255FDB87220; the preceding instruction reads RAX from
    `[rbp+0x60]`, the embedded active-pass list's next pointer.
-   Runtime RTTI identifies RAX as the **bhkCollisionObject virtual-function
    table**, at SkyrimVR RVA 0x15E23C8.
-   The later full dump's memory map marks the write target PAGE_READONLY
    (0x2), MEM_IMAGE (0x1000000), committed.

The game is treating a virtual-function table as a linked-list node. This
matches the fault address and bad pointer seen in the earlier crash.

The crash-time stack passes through vanilla batch cleanup/rendering,
`FrameAnnotations::BSShaderAccumulator_RenderBatches::thunk`, and
`Deferred::Hooks::Main_RenderShadowMaps::thunk`. Those CSX wrappers are
callers on the stack; their presence does **not** identify the corrupting
writer. This run's captured exception is in Skyrim, not an NVIDIA worker.
The earlier NVIDIA exception remains a separate observation.

## Why the manual dump initially looked unrelated

The live debugger's exact last event was:

```text
Last event: a53c.9050: Exit process 0:a53c, code c0000005
debugger time: Fri Sep 11 22:52:28.735 2026 (UTC + 2:00)
```

The 26,355,063,895-byte manual dump contains full memory but only one
remaining thread, in WININET during thread shutdown, and no exception
stream. It was captured at process exit. The synthetic breakpoint context
shown when opening that dump is not the original exception.

Windows independently retained a 268,137,732-byte crash-time dump at
`[REDACTED_LOCAL_PATH]`.
Its preserved copy contains the original exception context and the thread
stacks. The temporary WER dump referenced by Event 1001 had already gone;
the per-user CrashDumps copy was recovered instead.

The saved WinDbg log proves `sxe av` was enabled before `g`. It contains no
AV notification before process exit. Why that delivery was missed remains
unresolved; hidden-thread behavior or other mechanisms must not be reported
as established facts.

## Scope and timing

Initial Windhelm positioning and the ten-second wait completed. After an
earlier mistaken CTD report was retracted, the same setup was reused.
Scenario 2 accepted 126 steps for 25 measured transitions, with one
five-second pacing wait per transition and a strict 30-second waiter.

The first measured destination, Dragonsreach (cell 0x165A3), was observed.
Internal render-scale stability was logged at 22:52:11.519 after 13 frames;
this is **not** a retained strict-waiter measurement. Post-load recovery
completed at 22:52:12.032, with normal reported memory pressure, successful
trim and 429 displaced target references released.

Another loading-menu opening was logged at 22:52:17.276, consistent with the
next scheduled transition. Windows Event 1000 recorded the fault at
22:52:18.576. A dispatch receipt for that subsequent COC was not recovered.
No complete measured transcript survived the control-plane failure, so
there is no fabricated completion count, stabilization average or retry
total and no completed ledger column.

## Heap evidence and limits

The later full dump retains 0x00007FF6727923C8 at the source pointer slot
0x00000255FDB87280, amid repeated collision-object-like allocations.
The batch storage itself is committed PAGE_READWRITE private memory.
This is consistent with corruption or stale storage being reused for
physics objects. It is later heap evidence, not a complete snapshot of
the heap at the original fault.

The fault-time minidump does not contain the batch object's heap page.
Neither dump records the earlier allocation/free/write history needed to
identify its owner. Making the virtual-function table writable or merely
suppressing this invalid access would not establish a correct fix.

## Next investigation

Trace the creation, release and writes of the live batch-renderer object,
particularly its +0x60 list-pointer slot, before it becomes invalid.
Bind any watchpoint to a freshly observed object in the same process;
the addresses in this report belong only to PID 42300.

Resolve first-chance delivery before another reproduction. A process-exit
dump cannot substitute for fault-time capture; Windows' separate dump
proved essential here. A full fault-time backup capture would improve
heap coverage. Fix the owner/lifetime or out-of-bounds writer once the
trace identifies it, then repeat all 25 COCs at the specified pacing.

## Preserved evidence and validation

-   [Final summary](summary.json)
-   Fault-time dump (excluded from this export; see the bundle README)
-   [Fault-time exception and all thread stacks](crash-stacks.txt)
-   [Post-exit heap and runtime RTTI](post-exit-memory.txt)
-   [Post-exit memory protection](post-exit-memory-map.json)
-   Windows crash events (excluded from this export; see the bundle README)
-   Windows report (excluded from this export; see the bundle README)
-   WinDbg log with dump completion and exit event (excluded from this export; see the bundle README)
-   [DLL deployment verification](runtime-dll-verification.json)
-   [Manual full dump identity](dump-identity.json)

Fault-time dump SHA-256:
`AFFD0400EAE78C85DD193DF29EF8509514A679CCC148E02170A4D0D87361AC5F`.

Manual full dump SHA-256:
`4BF09D2E22F15287C20ACE4D56EC4C620CC89EE370E7D0FE41E8F77C1A49DE15`.

Build ID:
`fb078514153553f30d89552b2d469ebc41a372a82407cbe817abf15ef3ac664a`.

Source commit:
`5655a7102d02d6ab40b14888a52596c4c9e9f450`.

The enabled physical DLL, adjacent manifest, AIO receipt and runtime producer
agree; canonical provenance verification passed. Logs, WER report and the
fault-time dump were copied and hash-verified in the configured MO2 evidence
archive. The manual dump remains at its user-selected `[REDACTED_LOCAL_PATH]`
path. All original evidence is retained.

Offline CDB's `!address` command stalled on the small dump. That exact
offline reader was stopped, and stack extraction completed successfully
without the command. Live Skyrim and WinDbg were not resumed, detached,
restarted or terminated by the agent. No static Ghidra analysis was used.
No tracked source was changed; `git diff --check` passed.
