# NVIDIA SDK distribution

Streamline source and runtime inputs are pinned to the official NVIDIA
repository and release archive. Neural Rendering is a separate NVIDIA SDK
package referenced by the pinned RTX Remix revision in
`cmake/NeuralRenderingSDKPin.cmake`. Build-time SDK inputs must retain their
verified official source. The SDK pin
does not restrict the user-installed NR runtime to that package hash.

## Building and packaging

The normal build verifies both SDK archives before extracting its
compile-time inputs and the permitted Streamline runtime files. The NR
parameter header remains in the verified SDK build cache;
its original bytes are checked against the package hash. The full NVIDIA
RTX SDK license is retained unchanged under `extern/NeuralRenderingSDK/`.
Do not commit or republish the proprietary SDK headers. Streamline supplies the
matching NGX headers and static dispatcher library. NR retains the SDK's
parameter types, public setters and verified NR parameter definitions,
while initializing the admitted provider through its D3D12 snippet exports.
It does not invoke SDK initialization or feature discovery. Streamline
continues to use the CSX project identity for its own SDK integration.
The legacy route verifies the loaded file against the locked, hashed file.
Parameter allocation and destruction use one retained allocator from that
provider or an already loaded NVIDIA DriverStore core. A driver core must
pass physical path, loaded-file identity, version-resource and offline
Authenticode checks; adjacent parameter-core DLLs are rejected. Parameter
maps retire before provider shutdown and module release. A provider-local import adapter
supplies the NGX caller basename expected by snippet entry points. It is
restored before unloading; unsafe teardown retains module ownership.
Provider initialization and execution results remain authoritative.
DevBench reports the legacy route and that SDK discovery was not attempted.

`StreamlineRuntime` installs the Streamline production DLLs and all original
notices,
including the NR package's complete license under
`Shaders/Upscaling/Streamline/nvngx_dlssnr.license.txt`. The release guard
rejects every NR DLL, changed or missing licenses, changed Streamline
binaries and internal-only notices. NR object code must be installed by the
user separately; no build, installer or release may supply it. The verified
NR SDK cache supplies headers only and must never become a packaging input.
Keep NVIDIA components separately licensed;
the GPL linking and modding exceptions do not grant NVIDIA rights.

`SKIP_RUNTIME_DOWNLOADS=ON` can reuse verified cached NR inputs. The
verified SDK header cache must already be populated for offline DLL
development. Configure once with runtime downloads enabled to prepare it. Missing
Streamline runtime inputs block runtime installation and packaging. An absent
user NR runtime does not block builds or packages; it disables NR in game.
Compatible user-supplied NR versions 310.8 and newer retain the existing
version admission policy, with final availability checked by the provider's
own initializer. CSX does not impose an RTX generation check.
Runtime SHA-256 values identify the installed file for diagnostics; they
are not an allowlist. The cold probe compares the locked file's digest
with the runtime reference in `cmake/NeuralRenderingSDKPin.cmake`.
Logs and DevBench distinguish an exact reference match, no reference match,
and unknown identity. A mismatch does not establish that a DLL was modified;
an exact match does not establish GPU compatibility or its installation
source. Signature verification is reported as not checked. Native
initialization evidence distinguishes untested, failed and successful
legacy initialization, separately from the dispatch route. DevBench captures
identity, driver requirements and route under renderer lifecycle ownership
so a concurrent reset cannot mix those facts from different probes.
Existing runtime directories retain the ordinary
skip-mode deployment policy.

## NVIDIA license obligations

Read the complete license and RTX supplement supplied with each SDK.
Sections 1(c), 2 and 4 permit distributing SDK object code within an
application with material additional functionality, under terms at least
as protective as NVIDIA's. Preserve proprietary notices and the complete
terms with the binaries; do not distribute the SDK as a standalone product.

The SDK license restricts binary modification, reverse engineering and
circumvention of technical or authentication limitations. DLSS/NGX use is
restricted to systems with NVIDIA GPUs. The application must not imply
NVIDIA sponsorship or endorsement.

Before public distribution, the release maintainer must satisfy the RTX
supplement's attribution and trademark requirements. Proposed NVIDIA mark
usage requires prior written approval; the stated submission period is at
least two weeks, and silence is not approval. The supplement also requires
notification before a commercial release, explicitly including a plug-in
to a commercial application. Do not assume a free mod is exempt from every
obligation. Use the notification destination in the supplied license.

For applicability or permission questions, the license specifies
`nvidia-rtx-license-questions@nvidia.com`. Keep correspondence and approval
records private. A successful build or package check verifies technical
inputs and notices; it does not establish NVIDIA approval.
