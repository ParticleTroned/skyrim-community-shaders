# API service registry

CSX exposes new APIs through a versioned service registry while preserving the
existing `ICSInterface001` CSAP interface. Existing consumers do not need to
change, and new services must not add virtual methods to the legacy interface.

## Compatibility policy

- CSAP message type `CSAP` and `ICSInterface001` revisions 1–4 remain available.
- The service registry uses the separate SKSE message type `CSXR`.
- A service major version is an ABI break. Minor versions are additive.
- Clients request an explicit major and an inclusive minor-version range. The
  registry returns the highest registered compatible minor version.
- Service interface objects and registrations live for the CSX process
  lifetime. A temporarily unavailable service reports that state through its
  own interface rather than disappearing from the registry.
- Native interfaces use fixed-width values, caller-sized structures, function
  tables, and opaque pointers. STL containers, exceptions, RTTI objects, and
  renderer/game pointers must not cross the DLL boundary.

The public registry contract is in `include/VRAPI/CSserviceapi.h`.

## Discovery

After SKSE `kMessage_PostLoad`, a consumer dispatches
`CSX::ServiceAPI::RegistryMessage001` to `CommunityShaders` with message type
`RegistryMessageType`. A successful response contains a provider-owned
`Registry001` function table.

Clients should then:

1. Verify `Registry001::abiMajor` and that `structSize` contains every function
   they intend to call.
2. Copy `ProducerIdentity001`, especially Build ID and artifact SHA-256, into
   any capture or diagnostic record.
3. Enumerate descriptors for diagnostics or query a known service by name,
   major, accepted minor range, and required coarse capabilities.
4. Cast the returned opaque pointer only to the interface type defined by the
   successfully negotiated service descriptor.

No request uses `0` to mean "latest". Version acceptance is always explicit.

## Producer identity

The registry uses the canonical build-provenance implementation. The native
identity includes Build ID, loaded DLL SHA-256, source identity, manifest
verification, shader-cache ABI, and shader-compiler identity. Returned string
pointers are borrowed; consumers copy them before making another registry call.

DevBench services use the same information in their JSON response envelopes.
JSON schemas and native function-table ABIs are separate adapters over the same
domain controller; neither adapter owns domain behaviour.

## Service registration rules

Service names are stable lowercase identifiers such as `csx.upscaling` or
`csx.weather`. Registration requires a non-zero major version and a non-null,
process-lifetime interface. Duplicate name/major/minor tuples are rejected.

Coarse capability flags describe inspection, runtime mutation, persistent
mutation, destructive operations, asynchronous operations, events, and
transactions. Each service defines its detailed capabilities and block reasons
inside its own versioned interface.

The first domain service should establish the complete pattern:

- immutable snapshot structures;
- explicit configured/requested/effective/persisted state;
- structured result and block-reason codes;
- state revisions for optimistic concurrency;
- preflight for mutations;
- operation handles and events for asynchronous work;
- internal main/render-thread scheduling.

Legacy CSAP methods may later delegate to the same domain controllers, provided
their existing observable behaviour and ABI remain compatible.
