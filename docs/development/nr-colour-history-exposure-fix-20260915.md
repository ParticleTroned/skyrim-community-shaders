# Character history and bounded exposure capture correction

The live build 573286928a8d5923b1353e780f6db077141a8664 showed two consecutive
normal-DLSS engine frames on seven character insertion switches, followed by
stable both-eye NR. Its Build ID was
f1987b20c3154fb39c773d63c482bb55db6f4e9b4e5c022e6f5c0be624a392d7.
The right-eye readback correction did not regress: the final saved snapshot
had 25,574 attempts, 25,572 successful reads and zero fallbacks per eye.

## Character source lifetime

Two source-lifetime couplings were corrected. History invalidation cleared
the actor decisions used to author current-frame category pixels, even when
those pixels were preserved. Insertion transition blocking also prevented
character authoring/capture, although that guard belongs to evaluation.

History resets now retain valid admissions only for the exact observed
current frame, including negative admission decisions. Stale/future/invalid
entries are erased. Full invalidation still erases all admissions and source
metadata. Prepared masks and projection caches are rebuilt. Existing category
policy latching and source/dimension checks continue to reject incompatible
masks. The insertion guard still blocks evaluation and route claiming during
the transition frame; it no longer suppresses collecting source categories.

The current active game was used for a separate diagnostic before compiling
these changes. The approved CSXTest01 reload completed, NR readiness was true,
and two actors were admitted in both eyes. Four intensity-only changes at
fixed late insertion recovered on the next engine frame:

| Intensity | Change frame | First both-eye NR frame | Backend reset |
| --------- | -----------: | ----------------------: | ------------- |
| 0.79      |       117030 |                  117031 | false         |
| 0.80      |       117051 |                  117052 | false         |
| 0.79      |       117072 |                  117073 | false         |
| 0.80      |       117093 |                  117094 | false         |

These observations narrow the extra second frame to insertion switching;
they do not prove the updated DLL's transition duration. The exact caller
sequence behind every old-build empty plan was not traced.

## Exposure capture

The live HDR draw bound a single-mip 2x2 R11G11B10_FLOAT AvgTex at t2.
ISHDR BLEND samples its xy components and uses y/x as its exposure multiplier.
The old 1x1-only shape guard prevented any readback.

Capture now supports a bounded 1x1 or 2x2 view with one visible mip and
ordinary non-border sampling. All texels are retained in an immutable 5x1
FP32 snapshot: scalar summary first, then up to four row-major texel records.
A scalar is usable only if the GPU proves that every average/target pair
is identical, finite, positive and within the existing ratio limits.
Different values receive scalar validity 3 and status non_uniform_avgtex.
The zero-input unit fallback remains distinct from measured exposure.
No spatial average, prior-frame value or future-frame value is substituted.

View and sampler identities are included in frame ambiguity checks.
DevBench publishes dimensions, mip count, sampler filter/address modes,
per-texel evidence and scalarStatus. A 2x2 capture therefore provides either
a verified uniform scalar or concrete evidence that spatial handling is
required. It does not claim that the live four texels are uniform before
the rebuilt DLL measures them.

Single-mip and sampler restrictions follow the actual shader sampling
contract, including [D3D11 sampler addressing/filtering](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_sampler_desc)
and [Texture2D dimensions](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sm5-object-texture2d-getdimensions).
The WARP regression checks a nonzero MostDetailedMip explicitly.

## Validation scope and evidence

Added regression coverage exercises current-frame admission retention,
negative decisions, repeated/full invalidation, stale/future frames and frame
zero. Exposure WARP cases cover uniform and non-uniform 2x2 data, each changed
texel, tiny differences, equal ratios with unequal raw values, NaN/infinity,
the live packed format, unsupported extents, single-mip SRV selection and
multiple exposed mips. Preparation/reconstruction must preserve baseline for
a rejected spatial field. Sampler and view admission tests cover unsupported
addressing/filtering/layouts.

The implementation must be committed before compilation. Build/test outcomes
are recorded separately after execution; this document does not claim a
new-DLL live pass. SE/AE use the shared exposure path; character authoring
retains its existing VR runtime gate.

Raw local evidence is under:
C:/src/skyrim-community-shaders/build/validation/nr-live-20260915/history-exposure-fix

The short activity trace recorded 300,000 ms, reached its requested duration
limit, and retained an explicit 81,019 ms unrecorded tail before finalization.
Tracking frames 108403..118037 contain all four measured intensity transitions.
Its SHA-256 is
25707116f2659e0672152253249fbb8ca202236e1cd116ca3f87d020b17ee792.
Stop succeeded and final status is idle. The attached game remains running
with the previous DLL. No 30-minute recording or AIO validation campaign ran.

The next installed-DLL run must measure insertion recovery, actual 2x2
texels/sampler admission, and source-frame availability at both insertion
points. Early evaluation may precede the HDR draw. Captured exposure does
not establish NVIDIA's expected colour domain, correct image appearance,
physical headset presentation or clean performance.
