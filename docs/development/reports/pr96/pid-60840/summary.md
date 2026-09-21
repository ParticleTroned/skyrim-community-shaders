> Historical report, privacy-filtered for this export. Statements about
> current process state and planned work describe the original session.
> See the bundle README for later findings and omitted source files.

# COC diagnostic: repeated NVIDIA command-consumer hang

Six transitions reached strict completion. The seventh froze while leaving
Dragonsreach. Seven of twenty COC commands were dispatched; the next
admission failed with main_thread_timeout, and no further COCs ran.

| COC | Route                                      | Strict completion (ms) | Result           |
| --- | ------------------------------------------ | ---------------------: | ---------------- |
| 1   | WhiterunDragonsreach to WindhelmExterior01 |             10129.9342 | stable           |
| 2   | WindhelmExterior01 to WhiterunDragonsreach |              5446.4506 | stable           |
| 3   | WhiterunDragonsreach to WindhelmExterior01 |              2194.5291 | stable           |
| 4   | WindhelmExterior01 to WhiterunDragonsreach |              2533.5116 | stable           |
| 5   | WhiterunDragonsreach to WindhelmExterior01 |              2162.5271 | stable           |
| 6   | WindhelmExterior01 to WhiterunDragonsreach |              2649.4091 | stable           |
| 7   | WhiterunDragonsreach to WindhelmExterior01 |            unavailable | timeout          |
| 8   | WindhelmExterior01 to WhiterunDragonsreach |            unavailable | admission_failed |

Transitions 9–20 were not run. Five seconds were inserted before each
dispatch after admission; this was not a fixed wall-clock dispatch cadence.
Completion times exclude those waits. The seventh wait lasted
30000.5445 ms without strict completion.

## Confirmed mechanism

Ghidra decompiled code extracted from the full dump of PID 60840. CDB
private symbols identify CSX's original right-eye Submit call at
InSceneOverlay.cpp:972, called from line 1747. SteamVR calls the device
context Flush function at vrclient_x64+0x14D992. Flush waits for NVIDIA's
CPU command worker through semaphore 0x17CC. The worker is alive.

The command consumer at nvwgf2umx+0x598370 reads a 16-bit opcode and a
16-bit command length in dwords, dispatches that opcode, and advances by
length \* 4. At 0x2A409AE19D8 both fields are zero. The queue end is
0x2A409AE7EC0. Consequently the cursor never advances and completion is
never signaled. The current batch has 317 preceding valid-length commands.
The opcode-zero handler keeps emitting driver commands. A later live
sample still used the same packet while the main wait retry count grew
from 162 to 667. This is a CPU command-consumer loop, not proof of a GPU
hardware hang.

The earlier PID 5236 dump has the identical consumer code and a zero-length
packet at 0x18882C943F0, before end 0x18882C955C8. Its worker cursor
matches that address. Both freezes share this mechanism.

## Shadow fix and unresolved origin

The new shadow journal contains 251586 retained records and zero exceptions,
with no dropped/overwritten records or unclosed render scopes. The earlier
run recorded 1033 access violations. This is consistent with the light
ownership fix preventing the previously observed fault path in this run;
it does not prove every lifetime fault is fixed.

The submitted texture 0x2A4E7023CE0 is retained by the submission packet,
with device 0x29E655168B0, right-eye bounds [0.5, 0, 1, 1], and
publication generation 18. The captured native submission branch and
absence of a shadow exception do not identify the queue writer.

The dump contains the malformed queue after the write; it has no history
of the instruction that produced or overwrote its header. No originating
game, CSX, other-plugin, or driver defect is established. No speculative
queue repair or additional runtime fix has been applied. Capturing the
queue producer/write history on a new reproduction is the next causal
test; changing the already-invalid packet would not establish a fix.

## Preserved evidence

-   summary.json contains every available per-transition receipt.
-   dump-receipt.json hashes the 27091167008-byte full dump.
-   dll-identity.json verifies the enabled physical DLL, matching PDB and manifest.
-   ghidra-investigation/projects/FrozenSkyrim60840.gpr preserves the Ghidra project.
-   ghidra-investigation/nvidia-worker-ghidra.txt proves the loop and signaling path.
-   ghidra-investigation/driver-command-queue.json preserves the parsed current batch.
-   ghidra-investigation/previous-driver-command-queue.json preserves the old batch.
-   ghidra-investigation/verify-old-worker.log verifies the old cursor.
-   ghidra-investigation/inspect-worker-live.log verifies the repeated live wait.

The game was left running and frozen. Debugger attachments detached;
shadow/stress capture remains active in the frozen process. The journal
copy is coherent with zero active writers, but is not a stopped-session
flush confirmation.
