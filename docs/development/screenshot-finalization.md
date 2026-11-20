# Screenshot sequence finalization

Final manifest admission commits the worker queue entry, outstanding count,
generation and `packaging.queued` event together under the worker mutex. A
rejected admission consumes no generation or queued-event identity.

Finalization failures commit a terminal failure before attempting fallible error
JSON, logging or journal work. Storage custody is retained while already-admitted
children drain and released by the last child without allocating. A failed
parent cannot admit more frames, and later completions update accounting without
constructing new manifest snapshots or retrying publication. Burst encoding can
drain as soon as any current acquisition finishes.
Preallocated failure states and retained exception pointers make that cleanup
independent of allocation. A failed terminal event is not reported as an emitted
event; its request-local and service event IDs remain unconsumed. The next
successfully constructed receipt includes the retained finalization and reporting
errors. Its UTC timestamp can remain null if timestamp construction failed.

The pinned JSON implementation allocates a traversal vector when destroying a
nonempty array or object. Event construction therefore avoids structured
initializer-list temporaries and retains cleanup guards around owned payloads,
events and manifest preparation. Rollback drains queued headers before removing
their jobs. The cleanup utility repeatedly removes scalar or empty-container
leaves; it uses neither a heap traversal stack nor recursive calls. This keeps
the original allocation exception observable while the fault remains armed.

Guards are installed on empty owners before any structured fields are built.
Nested receipt, error and manifest objects are populated incrementally and
transferred by swap. Structured inputs are copied through an iterative guarded
tree builder; failed copies leave the destination unchanged. Append operations
move only complete guarded copies. This also covers producer, continuity and
reference-completion metadata called from the finalization path.

Reference completion receipts are prepared before callback ownership transfers.
If receipt allocation fails, the callback remains with the retained request for
a later drain. Successful preparation preserves terminal-transition order, and
the callback is invoked outside the coordinator mutex.

Acknowledgement, expiry and request-count trimming cannot erase a request while
it owns a reference callback or an active sequence child. A terminal failed
sequence remains pending for admission while its children drain. Provider status
keeps the failure visible with `done=false` until the last child completes; its
one terminal notification and terminal idempotent replay then carry the final
counts. Once children have drained and a ready notification owns the callback
and receipt, ordinary terminal retention applies again.

Worker-result draining handles each consumed result independently. Optional
packaging journal failures retain their exception without skipping a matching
final result; failures preparing its terminal metadata instead enter the hard
failure boundary. A preparation result cannot discard completions already
removed from the other worker lane.

A committed child-terminal transition retires parent in-flight accounting on
both successful and exceptional reporting paths. Accounting is idempotent and
does not allocate. Fallible child-manifest construction follows accounting and
fails the parent safely if it cannot complete. Source, artifact and queued
cancellation callbacks retain completion exceptions and do not consume a final
input without a terminal disposition. An artifact request with several outputs
continues counting the remaining callbacks before its failure disposition.

`ScreenshotFinalization` uses the actual coordinator class, production method
bodies selected by `extract_screenshot_finalization.cmake`, the real
`ServiceFoundation` journal and Windows directory leases. It injects allocation
failures at finalizing, queued, failure and terminal events, consecutive and
persistent failures, and terminal receipt construction. It checks queue and event
identities, terminal state, lease release, pending capacity and callback ordering.
An additional mixed-value, 256-level cleanup control keeps persistent allocation
failure armed and verifies that cleanup attempts no allocation. The three induced
stack-capture controls are separate from actual SDK stack capture and from the
finalization assertions. Four construction sweeps first measure each warmed
queue, terminal, receipt and notification operation, then reject every scalar
allocation in turn with subsequent failures kept armed. They check mandatory
state, storage, queue, generation, event and callback custody before clearing
the fault, then check journal identity and exactly-once callback recovery.
A separate mixed-value copy sweep checks partial-copy unwind and an unchanged
destination. These controls do not claim aligned, array or nothrow allocation
replacement coverage.
Fault-armed checks inspect stored scalar references directly and verify that
the check itself attempts no allocation. JSON-to-scalar equality can construct
a temporary JSON value inside a nonthrowing operator and is unsuitable here.
Object builders construct an empty object before indexing it. Implicit null-to-
container promotion can change the JSON type before allocating its storage;
allocation failure must not leave that partial value for cleanup to traverse.
Appending to a null array builds a guarded replacement and publishes it by swap.
Separate object and null-array sweeps reject each allocation with persistent
failure armed and require an unchanged null destination.
The fixture binds only coordinator lifecycle setup, inert producer metadata and
logging; it starts no I/O worker and writes no manifest or screenshot.
The terminal-custody cases additionally drain controlled worker-result inputs,
invoke actual source/artifact callbacks and queued cancellation, and apply
acknowledgement, expiry and capacity pressure before notification delivery.
The artifact callback cases use committed three-byte storage payloads; they do
not certify an image encoder or rendered screenshot.

Compile the non-default `screenshot_finalization_test` target through the owning
build recipe. Compilation and source-contract checks do not establish that its
assertions ran. The fixture is CPU and storage qualification, not GPU, screenshot
rendering or deployed-service validation.
