> Historical report, privacy-filtered for this export. Statements about
> current process state and planned work describe the original session.
> See the bundle README for later findings and omitted source files.

# 26-COC shadow diagnostic: interrupted

Requested 26 alternating COCs between WhiterunDragonsreach and
WindhelmExterior01, with a 5,000 ms server pause before each dispatch.
The batch issued three COCs and stopped on a main-thread control failure.

| COC  | Destination          | Result                                | Strict completion                            |
| ---- | -------------------- | ------------------------------------- | -------------------------------------------- |
| 1    | WindhelmExterior01   | Stable                                | 11,003.2635 ms / 31 frames                   |
| 2    | WhiterunDragonsreach | Stable                                | 6,694.1591 ms / 28 frames                    |
| 3    | WindhelmExterior01   | Timeout; operator confirmed freeze    | No completion; waiter elapsed 30,001.4135 ms |
| 4    | WhiterunDragonsreach | Admission failed: main_thread_timeout | Not dispatched                               |
| 5–26 | Alternating route    | Batch aborted                         | Not dispatched                               |

Timings start at the COC command and exclude the five-second pause. The
active shadow diagnostic adds overhead, so these are not performance results.
The scenario aborted after 13 steps and 67,857 ms.

The journal snapshot contains 219,225 records with no missing slots or
overwritten history. It records 1,033 first-chance access violations on COC 3,
starting at sequence 218154, thread 47468, address 0x33509950. The decoder
reported no lifetime candidates or unclosed render scopes. Its 204,428
generation misses limit object-identity conclusions; this does not establish
a shadow lifetime defect.

The full hang dump is 27,454,712,331 bytes. CDB reported a successful write,
exited with code zero, and detached. SHA-256:
`73b45e0a0589e0a445912c84a75dffe04393df55cdc19263f51a8e7525099964`.
The captured main thread waits in NVIDIA/D3D11 flush calls under SteamVR
submission. This observation does not establish the original fault's cause.

The enabled AIO DLL, its manifest, and the preserved DLL/PDB match build
`d1b5a347cf75b29973f8e5a8d22477ce3e4a112b057566f329a7c218da4a7e6d`,
compiled from dirty source at `befd358515eb201aa5a278292b93204f24c44d9e`.
No competing enabled loose DLL, Overwrite DLL, or unmanaged Data DLL was found.

Skyrim was left running. Stress session 1 and the shadow capture remain
active because no further main-thread calls were issued after the failure.
The preserved journal is a live snapshot with zero writers at snapshot time;
an orderly journal flush was not confirmed.

Evidence: `scenario-transcript.json`, `transitions.json`, `summary.json`,
`events.jsonl`, `events.summary.json`, `journal-snapshot-receipt.json`,
`dump-receipt.json`, `cdb-capture.log`, the `dump/` directory, and `producer/`.
The default collector lacked discoverable ProcDump/CDB and required more
free space; installed CDB captured this dump instead. Local tool-discovery
feedback: `AUTO-20260912-055228694-BAFE6B47`.
