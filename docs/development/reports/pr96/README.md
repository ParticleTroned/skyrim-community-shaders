# PR96 crash investigation and supporting evidence

This bundle preserves the detailed September 11–13, 2026 investigation
that led to PR96, `fix(vr): retain lights during native shadow rendering`.
It can be read and checked on another computer without the original
machine's folders, installed game, debugger, or process dumps.

The decisive finding is a native shadow light being destroyed by a
loading thread while its render call is still active. The connection
between that lifetime defect, the earlier render-batch crashes, and the
NVIDIA command-consumer hangs remains unproven. These are distinct
observations; the reports preserve their original uncertainty.

## Reading order

1. Read the [consolidated ownership and fix report](../../vr-shadow-light-lifetime-fix.md)
   for the implementation contract, VR-only scope, and validation limits.
2. Read the [original crash diagnosis](pid-54196/diagnosis.md) and
   [shadow-owner follow-up](pid-54196/shadow-lifetime-findings.md). They
   reconstruct the light → descriptor → accumulator → batch chain and
   explain why a later heap snapshot alone cannot establish the writer.
   The [earlier WinDbg case](pid-42300/diagnosis.md) independently captures
   the same invalid-write signature.
3. Read the [LLF use-after-destruction analysis](pid-5236/ghidra-investigation/findings.md).
   The journal supplies ordering that the earlier dumps lacked.
4. Read the [repeated driver-hang analysis](pid-60840/summary.md). The
   hang persists in a candidate with no recorded light-access exceptions.
5. Read the [direct PR96 investigation](pid-22880/analysis.md) and its
   [complete overlap timeline](pid-22880/shadow-overlap-timeline.json).
   This establishes the additional native consumer that PR96 protects.
6. Read the [first review](validation/review-1/REVIEW.md) and
   [second review](validation/review-2/REVIEW.md), then their adjacent
   validation receipts and focused build and test logs.

The copied reports are historical records. Statements such as “the game
remains frozen,” “next diagnostic,” or “no fix has been applied” describe
that report's original session. They do not describe the current machine
or supersede the later findings in this reading order.

## Findings and boundaries

| Case      | Observed failure                                   | What the evidence establishes                                                                                                             | Limit                                                                                                                      |
| --------- | -------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------- |
| PID 42300 | Write AV at `SkyrimVR+0x13480C8`                   | A corrupt batch-list pointer refers to the `bhkCollisionObject` vtable; fault-time stacks survive.                                        | The fault-time minidump lacks the batch heap page; the later full-memory snapshot is not a write history.                  |
| PID 54196 | Same write AV during the second measured COC       | Reconstructed shadow-light/descriptor/accumulator/batch chain; cleared owner fields in the captured heap.                                 | Clearing could have followed the fault. No corrupting writer is identified.                                                |
| PID 5236  | 1,033 first-chance AVs, then a presentation freeze | 138 faults resolve to LLF's second `IsShadowLight()` call; a known light's destruction ends 25.8 microseconds before the first exception. | The 895 execute faults lack the original RCX/return address. The driver-hang causal link is unknown.                       |
| PID 60840 | Freeze on COC 7 after six completions              | NVIDIA's command consumer encounters opcode zero with zero length and cannot advance; a later live sample confirms persistence.           | Zero journal exceptions does not qualify the candidate or identify the malformed command's writer.                         |
| PID 22880 | Freeze on COC 3; native render/destruction overlap | Generation 28 is destroyed during its render call, 0.6308 ms before return. Captured native code reads the destroyed light afterward.     | No relevant exception occurs. This proves use after destruction, not the allocator-free time or the driver-command writer. |

The three shadow journals retain **219,225**, **251,586**, and **274,638**
records respectively, with zero dropped or overwritten records. They are
coherent live snapshots with zero active writers at copying; orderly
stop/flush was not confirmed because the main thread was frozen. Their
generation misses and other limitations remain in `events.summary.json`.

All COC timings are diagnostic observations with instrumentation active.
This export adds no measurement, performance claim, stability pass, or
render-scale qualification.

## Build and implementation identity

| Case               | Compiled source                                                        | Producer Build ID                                                  |
| ------------------ | ---------------------------------------------------------------------- | ------------------------------------------------------------------ |
| PID 42300 / 54196  | `5655a7102d02d6ab40b14888a52596c4c9e9f450`                             | `fb078514153553f30d89552b2d469ebc41a372a82407cbe817abf15ef3ac664a` |
| PID 5236           | `befd358515eb201aa5a278292b93204f24c44d9e` plus recorded local changes | `d1b5a347cf75b29973f8e5a8d22477ce3e4a112b057566f329a7c218da4a7e6d` |
| PID 60840          | `feb1bee1f5a30a2c5fc0df114b5f9130cfe2cbe6` plus recorded local changes | `2e98f8f9cf442af4126744504be68631f02621fdaf67516487d9b99f82f9fedd` |
| PID 22880          | `8dcc88204ab4d6f871ef829ade4b9b6e40924636` plus recorded local changes | `e2fe386c8a467b1350165159286f746549f9f6883a51d5c23a2a37f8660ee75b` |
| PR96 validated DLL | `90d545008c1dda2b0d9e9d91b82021281698bbcd`                             | `cd983f022fb67aa4ff83b251aa5b63adaa499b6d44ea568089f1f4d83719e4fe` |

DLL hashes, sizes, original deployment checks, and dirty-source digests
are retained in each case's identity records. These are historical
verification receipts; no new deployment check is claimed. Dirty digests
identify source differences but do not reconstruct omitted working trees.

PR96 merged as `2e6d87cf763633917dbee54805476c7da0d2c995`. Its final
test/documentation revision did not change the validated DLL's production
sources. Native rendering retains active and pending owners under the
light-queue lock, copies the render order, and holds references through
dispatch. Complete native-function byte admission confines the patch to
the inspected Skyrim VR 1.4.15 layout. SE/AE receive no new native patch.

The preserved final review passed two focused tests, 744 native-hook
assertions, and 13 allocation-failure cases. It claimed **no in-game or
performance validation** of the final PR96 implementation.

## Included evidence

Each case directory contains its full selected reports, structured
findings, transition receipts, identity records, and supporting debugger
or Ghidra text. The JSON files preserve numeric observations, false, null,
and empty values. Identifying strings are redacted where required.

For the three journal cases, `events.jsonl.gz` contains **every decoded
record**, with unchanged field values. Compression avoids adding hundreds
of megabytes of repetitive text. Read it with Python's standard library:

```python
import gzip
import json
from pathlib import Path

bundle = Path("docs/development/reports/pr96")
with gzip.open(bundle / "pid-22880/events.jsonl.gz", "rt", encoding="utf-8") as stream:
    for line in stream:
        event = json.loads(line)
        if 274537 <= event["sequence"] <= 274626:
            print(event)
```

`tick` uses the QPC frequency in the adjacent `events.summary.json`:
elapsed milliseconds are `1000 * (end_tick - start_tick) / frequency`.
Sequence publication can interleave across threads; use ticks for temporal
ordering. Object addresses are process-specific correlation keys;
generation zero cannot exclude address reuse. Stack values are recorded
instruction addresses, not local filesystem or network addresses.

For PID 22880, compare light-render entry 274537, destruction completion
274583, and render return 274626. The separate native decompilation shows
the post-descriptor-render read at `SkyrimVR+0x137140C`. For PID 5236,
the lifetime correlation and exception-site transcripts resolve the LLF
faults. For PID 60840 and PID 22880, parsed driver queues and decompiled
consumer loops establish the zero-length/non-advancing mechanism.

## Privacy, integrity, and omitted files

This is a redacted export, not a byte-identical copy of the source
folders. Absolute paths, original local evidence/build paths, local
account and machine names, endpoints, session/ownership tokens, email
addresses, and identifying metadata are removed. Portable relative paths
refer only to this bundle or repository source. Sanitized strings use
explicit `[REDACTED_...]` markers. Original files remain unchanged locally.

All process dumps, raw binary memory/code/journal captures, DLLs, PDBs,
Ghidra databases, machine settings/caches, full game/CrashLogger/SKSE logs,
mod lists, and Windows WER reports are excluded. Existing extracted
disassembly, decompilation, stacks, typed object observations, and complete
decoded journal records carry the forensic evidence needed to understand
the findings. Reopening the original process image or resolving new
addresses requires separately obtaining the omitted dump and symbols.

The historical records state that the PID 5236 and PID 60840 full hang
dumps and an older manual WinDbg dump were deleted. Their analyses and
decoded journals survive here. PID 22880's retained dump is identified
by size and SHA-256 in its redacted receipt; no dump is committed.

[manifest.json](manifest.json) records original and exported SHA-256
hashes separately. Original hashes describe the unpublished source bytes;
exported hashes describe the redacted files after formatting. Changes to
formatting and privacy fields deliberately invalidate original file hashes.
Nested historical hash receipts still describe their original inputs.

Run the portable integrity and journal checks from the repository root:

```text
python docs/development/reports/pr96/verify.py
```

The verifier reads only this bundle. It checks exported hashes and sizes,
complete journal counts and event totals, sequence continuity, and the
native lifetime overlap. The publisher's independent source comparison,
privacy scan, and local-link audit are recorded in
[export-validation.json](export-validation.json). This validates the
report export; it does not rerun Skyrim or qualify the fix.
