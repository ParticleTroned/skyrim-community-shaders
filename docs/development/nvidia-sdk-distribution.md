# NVIDIA SDK distribution

Streamline source and runtime inputs are pinned to the official NVIDIA
repository and release archive. Neural Rendering is a separate NVIDIA SDK
package referenced by the pinned RTX Remix revision in
`cmake/NeuralRenderingSDKPin.cmake`. Build-time SDK inputs must retain their
verified official source; use only the CSX project identity. The SDK pin
does not restrict the user-installed NR runtime to that package hash.

## Building and packaging

The normal build verifies both SDK archives before extracting its
compile-time inputs and the permitted Streamline runtime files. The NR
parameter header remains in the verified SDK build cache;
its original bytes are checked against the package hash. The full NVIDIA
RTX SDK license is retained unchanged under `extern/NeuralRenderingSDK/`.
Do not commit or republish the proprietary SDK headers. Streamline supplies the
matching NGX headers and static dispatcher library. NR uses
`NVSDK_NGX_D3D12_Init_with_ProjectID` with the same CSX project identity as
Streamline, followed by the SDK capability, scratch, feature and teardown
APIs. If discovery reports a missing or unsupported NR feature, CSX can
initialize the admitted user provider through its D3D12 snippet exports.
This route verifies the loaded file against the locked, hashed file and
retains SDK ownership of parameter maps. A provider-local import adapter
supplies the NGX caller basename expected by snippet entry points. It is
restored before unloading; unsafe teardown retains module ownership.
Provider initialization and execution results remain authoritative.
DevBench reports the selected route and the original discovery result.

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
version admission policy, with final availability checked by the SDK or
the provider's own initializer. CSX does not impose an RTX generation check.
Runtime SHA-256 values identify the installed file for diagnostics; they
are not an allowlist. Existing runtime directories retain the ordinary
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
