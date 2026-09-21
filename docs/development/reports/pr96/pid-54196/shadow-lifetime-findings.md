> Historical report, privacy-filtered for this export. Statements about
> current process state and planned work describe the original session.
> See the bundle README for later findings and omitted source files.

# Shadow-owner lifetime follow-up, PID 54196

The dump now identifies the shadow light and descriptor that supplied the
damaged shader accumulator. It also exposes a native release path that can
clear and destroy that accumulator. This is a narrower investigation target,
not proof that this release path caused the crash.

## Evidence and limits

Analysis used the existing full process dump, captured runtime instructions,
saved exception/unwind state, runtime RTTI and matching local type layouts.
No static Ghidra analysis or live debugger attachment was used.
See `diagnosis.md` for dump provenance, exception and reproduction details.

Original fault: SkyrimVR+0x13480C8 attempts a zero write through a corrupt
batch-list pointer into the runtime bhkCollisionObject vtable. That invalid
pointer was present at the exception. Additional heap contents and other
threads were captured during crash handling; their ordering relative to the
exception is not established. They are not a record of earlier writes.

## Reconstructed render chain

| Stage                 | Address        | Evidence                                                      |
| --------------------- | -------------- | ------------------------------------------------------------- |
| Shadow scene node     | 0x26969FA6E80  | Saved render frame and scene-global read                      |
| Raw shadow-light list | 0x269105A4770  | Node+0x258, selector SkyrimVR+0x12FA250                       |
| Selected shadow light | 0x26E605EDD00  | Saved nonvolatile RBX in parabolic Render frame               |
| Descriptor 0          | 0x26E605EC800  | Saved helper RBX; caller computes array+index\*0x108, index 0 |
| Primary accumulator   | 0x26EE72809C0  | Helper reads descriptor+0x50; preserved descendant frame      |
| Root batch            | 0x26E605BD6C0  | Accumulator+0x158 in captured memory                          |
| Group 1               | 0x26E605CF040  | Root batch+0x78 in captured memory                            |
| Selected batch        | 0x26E605CF070  | Group first pointer and exception RBP                         |
| Corrupt list link     | 0x7FF6727923C8 | Batch+0x60 and exception RAX                                  |

The selected virtual Render implementation is SkyrimVR+0x1371330, slot
0x0A of the vtable at SkyrimVR+0x190C1F0. Captured RTTI identifies that table
as BSShadowParabolicLight. The damaged light's present first word is a heap
pointer, so the type attribution comes from its executing render function,
not an intact object header.

SkyrimVR+0x132321B calls the selected light's Render slot. Its callee calls
the common shadow helper at SkyrimVR+0x134C370. At +0x134C5E1 the helper
loads descriptor+0x50 into RDX; +0x134C5EC calls +0x12FF010, leading into
accumulator rendering and the failing batch cleanup.

In captured memory, both descriptor camera slots (+0x40/+0x48) and both
accumulator slots (+0x50/+0x58) are zero. The light's descriptor-array
pointer and count are zero, its reference-count field is zero, and its
underlying light and object-node pointers are zero. The scene node's ten
shadow-light slots are also zero. The saved render stack retains the old
light, descriptor and accumulator addresses. This pattern supports the
lifetime hypothesis, but some clearing could have happened after the fault.

## Native release paths verified from captured instructions

| RVA                | Observed behavior                                                                                                                      |
| ------------------ | -------------------------------------------------------------------------------------------------------------------------------------- |
| SkyrimVR+0x134B640 | Per-descriptor release, RCX=light and EDX=index; calculates descriptor from light+0x148                                                |
| SkyrimVR+0x134B6A4 | Clears descriptor+0x50 before atomically decrementing accumulator reference count                                                      |
| SkyrimVR+0x134B6B8 | Calls the accumulator's virtual release/deletion slot if the prior reference count was 1                                               |
| SkyrimVR+0x134B6CC | Clears and releases the primary camera                                                                                                 |
| SkyrimVR+0x134B6F4 | Clears and releases the second accumulator                                                                                             |
| SkyrimVR+0x134B71C | Clears and releases the second camera                                                                                                  |
| SkyrimVR+0x134CC70 | Descriptor destructor releases both accumulators and both cameras; unlike the earlier routine, it does not explicitly zero their slots |
| SkyrimVR+0x134B410 | Base shadow-light destructor calls per-descriptor release, destroys descriptor elements, and frees their array                         |
| SkyrimVR+0x13714B0 | Parabolic deleting destructor invokes the base destructor and conditionally frees the light allocation                                 |

The per-descriptor release returns early when descriptor+0x64 is already
0xFFFFFFFF. Its remaining path first marks that field unavailable, then
performs the clear/decrement operations above. The destructor separately
releases surviving owned camera/accumulator pointers.

The inspected ReturnShadowmaps path (+0x1371440 to +0x134B8F0) marks shadow
targets and allocation bits. It is not sufficient by itself to observe the
accumulator's last owning release.

The saved other-thread stacks do not identify an active caller of these
release routines. Absence from this later snapshot does not exclude an
earlier release. These routines' existence and matching cleared fields do
not establish which one executed against this object.

## Next diagnostic and the decision it must support

1. Prepare a bounded in-process lifetime journal around shadow-light render
   entry/exit, descriptor release/destruction, and actual accumulator
   destruction. Resolve creation/reinitialization too, so an allocation
   reused at the same address receives a different generation.
2. Record object relationships, generation, thread ID, monotonic timestamp,
   frame and COC transition. Capture the release caller's bounded native
   stack and observed reference count before the original operation. Mark
   the original exception boundary so later crash-handler activity can be
   excluded. Keep logging bounded and independent of the objects' lifetime.
3. Verify hook signatures and existing trampolines against the deployed VR
   runtime before implementation. The common helper already has a CSX
   camera guard; another entry patch must account for that ownership.
   Instrumentation must preserve original calls and reference operations.
4. Repeat the Windhelm/Dragonsreach reproduction with that journal and the
   existing CrashLogger full-dump capture. Correlate the same allocation
   generation across render use and destruction, including a release that
   occurs while a render call is still active.
5. A confirmed final release before last render use identifies the caller
   and missing lifetime/synchronization boundary to fix. If damage occurs
   without a preceding tracked destruction, extend capture to the writes
   that change the accumulator/batch fields. If clearing occurs only after
   the exception, do not attribute the original corruption to that clearing.

This journal has not yet been implemented or run. No fix, PR attribution,
rollback, rebuild, deployment or runtime validation is claimed. Existing
hardware-breakpoint capture that caused a separate failure remains withdrawn.

## Raw follow-up evidence

-   `shadow-owner.txt`: saved frames and initial owner inspection. The three
    `.fnent @rip` outputs resolve the exception RIP again and must not be used
    as caller-function evidence; explicit RVAs correct this in the next file.
-   `shadow-descriptor.txt`: explicit caller functions and descriptor memory.
-   `shadow-list.txt`: selector, scene node and shadow-light list.
-   `shadow-release.txt`: parabolic destructor and ReturnShadowmaps.
-   `shadow-destruction.txt`: base light destruction and RTTI locator.
-   `descriptor-lifetime.txt`: clear/decrement/delete instructions and
    captured BSShadowParabolicLight RTTI name.

Each file has an adjacent `.wds` command script and `-stdout.txt` transcript.
