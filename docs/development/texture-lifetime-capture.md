# SE/AE texture lifetime capture

Developer builds with `DEVBENCH_BRIDGE_ENABLED` expose
`communityshaders.texture_lifetime`. Capture is opt-in and attaches a private
COM sentinel to successfully created textures on the hooked game D3D11 device.
The sentinel observes final release without retaining the texture.

## Command contract

Every request contains `contractMajor: 1`, an optional integer
`contractMinor: 0`, a nonempty `clientId`, a unique `commandId`, and an `action`.
`expectedBuildId` can require the loaded producer reported by the build
provenance service. Reusing the same client/command pair replays its result;
use a new command ID to obtain a fresh status.

Start a capture with:

```json
{
  "contractMajor": 1,
  "clientId": "texture-investigation",
  "commandId": "start-1",
  "action": "start"
}
```

The response contains `result.capture.sessionID`. Use that integer as
`expectedSessionID` for `checkpoint`, `stop`, and `reset`. `status` and `start`
do not require it. A checkpoint advances the cohort assigned to subsequent
creations. Start rejects an active capture. Stop freezes its statistics;
reset accepts only a stopped capture. A later start begins a fresh session.
Sentinels retained by old textures cannot change a later session.

All commands use the existing main-thread task dispatcher. Mutations validate
capture ownership while holding the tracker mutex. An unavailable or timed-out
dispatcher reports an error without a late queued mutation.

## Interpreting the result

- `createdCount`, `destroyedCount`, and `outstandingCount` describe successfully
  tracked D3D11 textures created during the capture. Capacity drops, attachment
  failures, and recording failures are explicit. The tracker stores at most
  4,096 descriptor/stack/cohort groups and 16,384 live texture records.
- Estimated bytes derive from dimensions, block format, mips, arrays and sample
  count. They do not measure driver heap residency or allocator overhead.
  Unsupported formats contribute to the unknown-estimate count.
- Native ownership enrichment scans reachable world and preview materials on
  the main thread while holding the existing renderer transaction. It uses
  typed SE/AE accessors, a 16,384-node limit, a 32,768-source-texture limit, and
  a four-millisecond traversal work budget. It retains scene objects only for
  that scan. Invalid inventory, contention, world transitions, exhaustion and
  scan failures are visible in the result.
- `niSourceTextureCoverage` describes this reachable-scene coverage. An
  unmatched resource has **unknown ownership**. Materials outside those
  roots, or beyond a scan limit, are not evidence of a leak.
- `faceGenOwnerObservation` uses the typed native FaceGen material's
  `tintTexture` and, when available, its reachable FaceGen node's actor handle
  and NPC. `headPart` and `headPartFormID` remain null because the SE/AE port
  does not instrument a verified native head-part assignment callsite.
- `nativeSourceCreationObservations` contains historical metadata copied from
  the native source-texture creation callback. It retains no native or COM
  references, follows the tracked D3D texture's lifetime, and publishes at
  most 256 samples per result. It does not prove current native ownership.
- `statisticsFrozen` is true after stopping. Current native ownership scans
  are omitted for stopped captures, whose recorded addresses may subsequently
  be reused. Active observations carry their observation time.

This hook covers the game D3D11 device. SDK/driver allocations and D3D12
resources belonging to neural rendering or frame generation are outside its
coverage.

## Validation

Portable checks compile the production COM sentinel, lifecycle, byte estimator,
status serializer and DevBench command handler with deterministic COM and
native-traversal fixtures:

```sh
CSX_JSON_INCLUDE=/path/to/include python tests/d3d_texture_lifetime_test.py
g++ -std=c++20 -Wall -Wextra -Werror -I src tests/scene_inventory_test.cpp -o scene-inventory-test
./scene-inventory-test
```

The first command requires `nlohmann/json.hpp` and a C++20 compiler. `CXX` can
select GCC, Clang, MSVC or clang-cl. These checks cover rollback, capacity bounds,
session isolation, malformed requests, scan failures, capture changes during
scan and frozen ownership reporting. They do not execute Windows COM, SEH, the
native renderer lock, or the game's scene accessors.

For native validation, build the developer plugin on Windows and exercise both
SE and AE: start in a loaded world, capture status, checkpoint, load an inventory
preview and character with FaceGen materials, capture status, close those views,
and capture status again. Confirm bounded ownership records and explicit
coverage flags, then stop and reset using the returned session ID. Repeat across
a save/load transition and verify that native enrichment skips unsafe world
state while global lifetime counts remain coherent. A stale session ID must
fail without mutating a subsequent capture. Verify frame generation and neural
rendering still run normally; their D3D12 allocations are intentionally outside
this D3D11 diagnostic.
