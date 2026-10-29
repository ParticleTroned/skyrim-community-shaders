# Screenshot sequence finalization

Final manifest admission commits the worker queue entry, outstanding count,
generation and `packaging.queued` event together under the worker mutex. A
rejected admission consumes no generation or queued-event identity.

Finalization failures commit a terminal failure and release the sequence-owned
directory lease before attempting fallible error JSON, logging or journal work.
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

Reference completion receipts are prepared before callback ownership transfers.
If receipt allocation fails, the callback remains with the retained request for
a later drain. Successful preparation preserves terminal-transition order, and
the callback is invoked outside the coordinator mutex.

Acknowledgement, expiry and request-count trimming cannot erase a request while
it owns a reference callback. Once a ready notification owns the callback and
receipt, ordinary terminal retention applies again.

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
finalization assertions.
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
