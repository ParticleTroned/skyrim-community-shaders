# Shader compatibility API v1

`csx.shader.compatibility` is the versioned registration and inspection service
for external inputs that change generated shader bytecode. It is deliberately
separate from the main-thread `csx.shader` controller: providers may register
from their normal SKSE startup callbacks, and registration never calls the
renderer or game objects.

The native ABI is `include/VRAPI/CSshadercompatibilityapi.h`. Discover it through
the CSXR registry and query major version 1. CSX copies every registration and
scope before returning.

## Provider contract

A provider owns a stable, lower-case identity such as
`org.example.water-integration`, and reports:

-   a shader-facing contract major and current/minimum/maximum compatible minor;
-   an optional resource fingerprint when external shader inputs have identity
    beyond the contract version;
-   one or more declarative scopes: shader family or global.

Resource fingerprints and shader-family names must not contain ASCII control
characters. These values participate in the line-delimited canonical contract.

The ABI reserves shader-source and feature scope values, but version 1 rejects
them explicitly. Offline pack generation does not yet retain authoritative
per-record source and feature provenance for every ImageSpace remapping, so
accepting either scope would make the provider's stale-cache protection false.

`displayVersion` is diagnostic only. Updating a mod package without changing its
shader-facing contract must not invalidate shaders. Conversely, a shader-facing
change must update the contract or resource fingerprint even when the package
version does not change.

Register during the provider's `PostLoad` handling (or earlier after obtaining
the CSXR registry), not `PostPostLoad`; listener order at `PostPostLoad` cannot
guarantee that a provider runs before CSX freezes the set.

Registration is atomic. Repeating an identical registration is idempotent and
returns the original handle. Reusing an identity for different requirements is
an identity conflict. Providers must register before CSX validates its shader
cache; the registry then freezes. A late new registration is rejected with
`restartRequired`, while an identical replay remains successful.

## Cache identity

For each shader, CSX selects registrations whose scopes apply, sorts them by
identity, and serializes their exact shader-facing contracts into a canonical
requirement set. The record's logical identity contains the stable provider
domain (identity, major contract, resource fingerprint, and scopes), while its
exact identity also contains the current and supported minor range. A
Water-only provider therefore cannot invalidate Grass or Lighting records.

An exact lookup is attempted first. If it misses, CSX searches retained records
in the same provider domain and accepts the newest record whose supported minor
range overlaps the current range. A major-contract, resource, scope, source,
compile-state, or feature-ABI change still causes a miss. Compatible and
incompatible versions may coexist until ordinary pack compaction retains the
newest record for each logical shader identity.

The canonical data, not a friendly label or timestamp, is authoritative.
Digests are lookup accelerators and corruption checks. Pack records retain the
canonical requirement set so collisions or tooling disagreements can be
detected rather than silently accepted.

## Offline/precompiled packs

The full-build cache generator consumes the same fields through its declarative
JSON manifest. Each requested compatibility variant is compiled into the same
startup pack under its distinct canonical requirement set. Runtime selection
uses the active registration set; installers do not need a compatibility FOMOD
for variants that can coexist.

The initial Water/Horizon compatibility remains a legacy adapter until its
provider adopts this API. It registers only when `HorizonFix.dll` is loaded
and CSX's Horizon Fix feature is enabled. Both Water variants ship together
for SE/AE and VR, so installing or removing the companion plugin requires a
restart but no Horizon-specific FOMOD selection or cache reinstall.
Horizon Fix returns an empty feature ABI because this provider already owns
its contract. Its toggle therefore leaves non-Water content identities intact.
Other loaded feature ABIs remain conservative inputs to every shader family's
content identity.

## Managed cache storage

Optimized and developer/debug shaders occupy separate two-generation A/B pack
lanes. The files are shipped by the managed cache mod and are never created,
renamed, or deleted at runtime. Writes append committed records to the active
file. Before the main menu, fragmentation-based compaction rewrites the latest
record for every logical shader identity into the inactive file, durably commits
it, and selects its higher generation; the prior file remains a searchable
fallback generation. Fragmentation is measured only within the active file, so
the intentional fallback generation does not cause endless compaction. If
inactive-member mutation cannot complete, the Store withdraws its authority and
releases ownership before the caller quarantines that lane.

The managed layout is authoritative only when the manifest and all four fixed
files are present and the manifest passes the same strict identity, runtime,
format, variant, file-entry, lane, generation, and record-count contract used by
packaging. No managed members means the established loose cache remains active.
Authority is latched only after the manifest and all four fixed regular files
open successfully, their headers and identities validate, and their per-file
state satisfies the manifest contract. A partial, unreadable, non-file,
malformed, or otherwise invalid layout is diagnosed explicitly and fails closed
to source-only compilation until the installation is repaired or cleared. It
never creates a loose-cache tree beside installed managed members. Admission is
read-only: zero-byte placeholders are rejected rather than initialized, and a
global rejection releases every provisional lane lease without changing any
member. Direct lazy admission through append or reset has the same cleanup
boundary: a rejected or exceptional admission releases its path guards,
physical identities, process registration, and writer lease. Release tooling
must ship all four files with valid headers.

Every writable pack set has a nonzero 128-bit identity. Runtime header checks
are unconditional, and mutation ownership combines canonical process-local
exclusion with crash-recoverable machine-wide Windows named-pipe leases. Each
physical member has its own lease, independent of `TEMP`, argument order, lane
labels, and hard-link aliases. Overlapping A/B pairs cannot share a writer.
Physical identity is
mandatory on Windows: directories, reparse points in any path component,
identity-query failures, and same-object A/B pairs are rejected. Admission
resolves relative names once and retains non-delete-sharing handles for every
ordinary parent plus the final files. Later path-based I/O therefore reaches
the admitted objects or fails closed without adding identity work to shader
lookup. All four fixed members must have distinct
identities. The lease handle may be released from another thread and is
reclaimed if the process terminates. Optimized and developer lanes open and
validate independently during admission; any global rejection destroys both
stores and releases their leases, including after an exception. After a complete layout becomes authoritative,
an operational failure quarantines only the affected lane. An unavailable
authoritative lane recompiles from source without consulting or writing legacy
loose blobs.

`PackManifest.json` is immutable installation metadata. Schema 2 requires
`fileStateSemantics: installation-baseline-v1`; its schema, format,
runtime, seed shader ABI, identity, variants, fixed filenames, lanes, aggregate
counts, and each file's generation/count describe the shipped baseline. Runtime
appends may increase the count at that same generation. Compaction and reset may
advance a file generation and may reduce its count; they do not rewrite the
manifest. Runtime, archive, and FOMOD admission therefore require each actual
file to be at or beyond its named baseline, reject count regression within the
baseline generation, reject identity/lane mismatch and equal A/B generations,
and accept a later generation even when its count changed. All numeric manifest
fields are non-Boolean unsigned 64-bit integers; schema and format values must
also equal their supported constants. Each installation-baseline A/B generation
pair must differ by exactly one. Record sequences use the common domain
`1..UINT64_MAX-1` and increase strictly within each file; zero, `UINT64_MAX`,
duplicate, and decreasing sequences are rejected by both the runtime scanner
and packaging validator.

Explicit cache clearing first commits a new empty generation barrier, then
reinitializes the superseded file, rather than creating or deleting VFS entries.
The result distinguishes complete cleanup, committed-but-degraded cleanup, and
failure before commit. Cleanup-only degradation keeps a successfully reopened
empty generation authoritative. If either post-barrier reopen fails, the Store
invalidates every pre-reset index, releases ownership, and quarantines the lane
for source fallback until a clean admission succeeds. Reset-target initialization
tracks whether mutation has not begun, has begun with uncertain durability, is
durable, or has been verified. Only the first state may preserve the old Store
and report failure before commit; every changed or uncertain state clears old
authority and releases ownership.

The manifest's shader ABI is seed provenance, not a container-admission gate.
The ABI is already embedded in every record's content contract, so admitting an
older container preserves its reusable records and permits new records to be
appended alongside them. Packaging still requires a newly generated pack,
`Info.ini`, and the core build manifest to agree before release.

This compatibility transition is `engine-cache-v4-compatible-records` in
`config/shader-cache-abi.json`. A distributed managed cache contains exactly six
root files: `Info.ini`, `PackManifest.json`, and the four A/B pack members.
