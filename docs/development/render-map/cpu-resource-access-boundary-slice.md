# CPU resource-access boundary slice

## Purpose

This slice closes the missing CPU side of the immediate-context resource graph.
It observes D3D11 `Map` and `Unmap` calls without copying mapped contents or
retaining application pointers. The resulting events establish when a mapped
resource becomes readable by the CPU, when CPU-written content is published
back to D3D11, and how long the blocking `Map` call itself took.

The instrumentation is diagnostic and capture-bounded. It is inactive outside
an explicit render-map capture and it never changes the application's D3D11
arguments, return values, or mapped memory.

## Runtime contract

Render-event 1.14 introduces `resource-cpu-access-v1`.

For every immediate-context `Map` observed during one capture generation, the
event records:

- the exact capture-local resource identity and subresource;
- map type and flags, including `D3D11_MAP_FLAG_DO_NOT_WAIT`;
- the returned HRESULT and whether it succeeded;
- elapsed query-performance-counter ticks around the real `Map` call; and
- row and depth pitch when a successful call returned mapped metadata.

A successful readable map (`READ` or `READ_WRITE`) establishes
`cpu-readable-after-map-return`. This is a CPU-visibility boundary for that
resource/subresource. A failed map, including `DXGI_ERROR_WAS_STILL_DRAWING`
from a nonblocking request, establishes no visibility and creates no graph read.

Successful maps are paired capture-locally with the next `Unmap` on the same
immediate context, resource allocation, and subresource. The `Unmap` event
inherits the map type, flags, and pitches and reports the mapped lifetime. A
matched writable map (`WRITE`, `READ_WRITE`, `WRITE_DISCARD`, or
`WRITE_NO_OVERWRITE`) establishes `gpu-visible-after-unmap-return` and creates a
new whole-resource version in the current graph model.

An unmatched `Unmap` remains an event with a null map identity. The graph emits
a nonblocking incomplete-capture gap rather than inventing a map or a write.
Map pairs are cleared at capture start and are generation checked, so a map
which began outside the capture or in a previous capture cannot be joined to a
later `Unmap`.

## Timing interpretation

`durationQpcTicks` on a Map event measures wall-clock time spent inside the real
D3D11 `Map` call. For a blocking read map this includes any wait needed before
the driver returns CPU-readable data, plus ordinary call overhead. It is the
measured boundary needed for the Skyrim OBB readback path, but it must not be
described as pure GPU execution time.

`durationQpcTicks` on a matched Unmap event measures the lifetime from successful
Map return through Unmap return. It describes how long the application retained
the mapping; it is not a GPU-stall measurement.

The graph exposes aggregate Map call ticks, the maximum observed Map call, and
microseconds derived from the capture manifest's frozen QPC frequency. Timing
claims still require a usable performance context. Ordering and visibility
facts remain usable when frame pacing is unsuitable for performance comparison.

## Derived graph rules

Graph 1.11 / `static-semantic-resource-graph-8` models each Map or Unmap as a
`resource-operation` node:

- a successful readable Map reads the current resource version;
- a matched writable Unmap writes the next resource version;
- paired Map and Unmap operations receive a `precedes` edge with
  `CPU-map-lifetime` hazard provenance;
- failed maps create no read and unmatched unmaps create no write; and
- every resource edge retains the exact runtime event sequence.

The current version model remains allocation-wide. The event records the exact
subresource so a later graph revision can narrow CPU read/write versioning
without changing the capture contract.

## Validation state and next live gate

The runtime pairing, serialization, schema, and graph behavior are covered by
deterministic tests for successful read, writable discard, failed nonblocking
Map, and unmatched Unmap. The next live qualification should capture a loaded
scene with depth-culling diagnostics enabled and verify:

1. the engine staging-copy Map appears as `READ` on the statically identified
   resource;
2. every successful engine read Map has a matching Unmap;
3. measured Map duration is retained without capture loss; and
4. the resulting graph joins the staging copy, CPU read boundary, and published
   culling-result consumption in command order.

That live test is intentionally separate from implementation validation and
must wait for an available MO2/Skyrim test session.
