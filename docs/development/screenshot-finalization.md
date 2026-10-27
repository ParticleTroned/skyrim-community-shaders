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

Reference completion receipts are prepared before callback ownership transfers.
If receipt allocation fails, the callback remains with the retained request for
a later drain. Successful preparation preserves terminal-transition order, and
the callback is invoked outside the coordinator mutex.

`ScreenshotFinalization` uses the actual coordinator class, production method
bodies selected by `extract_screenshot_finalization.cmake`, the real
`ServiceFoundation` journal and Windows directory leases. It injects allocation
failures at finalizing, queued, failure and terminal events, consecutive and
persistent failures, and terminal receipt construction. It checks queue and event
identities, terminal state, lease release, pending capacity and callback ordering.
The fixture binds only coordinator lifecycle setup, inert producer metadata and
logging; it starts no I/O worker and writes no manifest or screenshot.

Compile the non-default `screenshot_finalization_test` target through the owning
build recipe. Compilation and source-contract checks do not establish that its
assertions ran. The fixture is CPU and storage qualification, not GPU, screenshot
rendering or deployed-service validation.
