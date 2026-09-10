# PR 71 adversarial review

This review challenges PR 71 against base `8ab94e447`, including the original
`889423e69` implementation and the four follow-up commits ending at
`bcdadd5e2`. The review covers runtime persistence, compatibility identities,
managed-pack recovery, packaging, and the earlier Horizon Fix installation
design. It does not qualify rendered output in a running game.

## Confirmed defects

### Compiled bytecode could acquire a different source identity

Deferred persistence reconstructed the source digest when writing the blob.
A root or include edit between compilation and the write could therefore
label old bytecode with the new source contract. The previous source-epoch
locking protected individual traversals, but did not preserve a compilation's
identity across the queue. A first compilation could also race a watcher
notification before a tracked shader existed to advance the disk generation.

Compilation now captures its source digest and checks the closure again with
fresh reads after the compiler returns. A detected change or failed closure
verification keeps the blob memory-only. Immediate writes, queued writes,
and feature-set commit retain the captured source digest, compile-state
digest, and developer lane.
Feature-set commit also uses the blob retained with that record, so a newer
shader-map entry with the same key cannot be paired with older metadata.

`ShaderSourceProvenance` exercises edits to roots and includes with preserved
timestamps, unavailable inputs, read exceptions, and compilation failure. It
also verifies captured digest retention after a later edit; it does not invoke
the actual deferred writer or feature-set commit. The native build checks
every changed runtime call site. Pre/post reads are not an atomic snapshot of
compiler inputs and cannot detect an edit followed by restoration between
those reads. This does not replace an in-game compilation/hot-reload stress
test.

### Horizon declarations did not prove installed coverage

Four empty but structurally valid packs could claim both `default` and
`legacy-horizon-fix` and pass FOMOD admission. A valid binary checksum and
manifest declaration did not prove that any usable Water pair existed.

Export validation now reconstructs record identities and canonical metadata,
then checks the visible permutation inventory for each declared variant.
Missing counterparts and declaration-only support are rejected. The identity
constructor is shared with pack emission so verification does not maintain a
second format implementation. Single-variant named profiles remain distinct
from release packages that promise both Horizon states.

The release assembler's sparse checkout includes the compatibility catalog
required by this validation.

Verification also requires byte-exact canonical metadata and at least one
changed Water payload across the pair, matching the compilation-stage delta
check. A relabeled copy of the standard cache cannot establish Horizon support.
Additional source contents may coexist when each path still retains complete
paired coverage under a common content contract.

This check is separate from runtime container admission. Runtime appends,
compaction, and reset retain the existing installation-baseline semantics;
an updated container still needs actual complete variant coverage to qualify
as a release archive.

### Unicode fingerprints produced different compatibility domains

Python's `splitlines()` treated U+0085, U+2028, and U+2029 as line boundaries.
The C++ canonical serializer preserves those valid fingerprint characters.
Consequently, an accepted provider could receive different offline and
runtime domain digests, preventing reuse of its packed records.

The Python domain serializer now splits only on the literal LF delimiter
used by C++. Regression cases preserve all three characters. ASCII controls
remain rejected by the existing provider contract.

## Horizon branch comparison

No branch named `cs-1-1-pl-vr` was present in the inspected local or remote
refs. The closest current branch, `cs-1.7-PL-VR`, was inspected at remote
`2051e2aead1b2bb2b03faa421201376e8bc84fe0` and local
`5b2fb2aec8867ec34a4c12e9163821a27969d11a`. Both retain the earlier
default-disabled Horizon cache builder. `cs-1.4.11-PL-VR` has no HorizonFix
feature header.

The relevant design history is already in PR 71's ancestry:

| Source                                                                                                                                                 | Rule retained in PR 71                                                                                                                       |
| ------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------- |
| [`64c6a3620`, optional companion handling](https://github.com/ParticleTroned/skyrim-community-shaders/commit/64c6a3620d126fb30a7ed9a8942777b157e5b988) | Detect the companion after plugin loading; its absence is supported, while genuine feature-manifest failures remain failures.                |
| [`f143434bc`, portable Horizon profiles](https://github.com/ParticleTroned/skyrim-community-shaders/commit/f143434bc380adc00e18e69f33286d815789c60c)   | Compile both states for SE/AE and VR with matching inventories and a Water-only bytecode difference; retain the SE-only permutation overlay. |
| [`d9ba7c458`, manual four-cache FOMOD](https://github.com/ParticleTroned/skyrim-community-shaders/commit/d9ba7c458c3a74aab9178bda018672618b08031c)     | Keep runtime selection explicit and independent of mod-manager DLL/marker detection; validate before replacing the release archive.          |

The old four-cache installer needed a second +/- Horizon page because only
one loose blob could occupy each shader path. Managed packs retain both
compatibility identities together, so the runtime selects the active Water
record. The current FOMOD therefore keeps VR, SE/AE, and no-cache choices on
one page. Reintroducing installer detection or the second Horizon page would
add an independent state selection that can disagree with the loaded game.

The branch's Horizon Water code and optional dependency handling already
exist in the PR base. This review introduces no HLSL rendering transplant,
companion DLL, or unrelated branch merge. The useful additional lesson is to
verify actual paired artifacts at export, not merely trust the selected
installer option or manifest label.

For development and testing, FOMOD generation also accepts
`--no-include-se-ae`. This omits the SE/AE input requirement, staged cache,
installer choice, and mapping together; it retains both Horizon records in the
VR cache. The default remains a two-runtime release package. This selects the
package's supported runtimes at generation time, independently of the game's
Horizon provider selection.

The manual **Release: Build Artifacts** workflow exposes the same choice as
`include-se-ae`, including conditional compilation, artifact download, and
archive checks. Automatic tag/release runs retain both runtimes.

## Reviewed policy boundaries

-   Native Windows tests exercise physical-file writer exclusion, hard-link
    aliases, overlapping pairs, failed publication, and reset cleanup. No new
    pack-store defect requiring a format change was established in this pass.
-   Historical minor-version records remain reusable until ordinary compaction
    supersedes them. Indefinite retention is not promised.
-   Repeated interrupted compactions may discard recoverable cache entries and
    require source recompilation. Failed mutations withdraw store authority;
    shader bytecode must never be reused under an incompatible identity.
-   Non-Horizon feature ABIs remain conservative global content inputs. The
    Water-scoped provider owns Horizon's ABI; broader feature-family salt
    changes require an authoritative runtime/offline mapping.
-   A source digest and record checksum establish cache identity and integrity,
    not publisher authenticity.

## Validation boundary

The PR description records the final build, native CTest, shared contract
corpus, Python archive/FOMOD tests, and scoped formatting results for the
published revision. The new export regressions challenge correctly hashed
records with missing variants or inconsistent canonical identities, beyond
ordinary corrupted-byte fixtures.

Loaded-scene cold/warm tests across SE, AE, and VR, Horizon plugin/feature
state changes, and full release cache generation remain separate
qualification steps. The fresh source read adds work only to actual
compilation; this review makes no measured performance claim.
