# main-SE validation record

Date: 2026-11-01. Worktree branch: `main-SE`.

- SE parent: `e1989826771a1faf9fbb9e7e77a3c38e2256051d`.
- Imported/adapted source: `f140c878138fe2783a6a4ad40420a3a2a996ccd0`.
- Implementation and limitations: [main-se-port.md](main-se-port.md).
- Changes are local and uncommitted at this checkpoint.

## Aggregate result

**84 / 84 CTest entries passed. Zero failed, disabled or skipped entries.**
The final combined run took 13.06 seconds with four parallel workers after a
fresh configure and successful full build of the portable targets, followed by
an incremental build after the UI changes were frozen. Its JUnit output was checked
for nested Python test skips as well as CTest-level skips.

This includes the preserved SE baseline executable, which ran **42 cases and
25,132 assertions** using genuine Catch2. Those 42 cases are contained in one
of the 84 CTest entries; they are not an additional 42 CTest executables.

| Validation group | Evidence |
|---|---|
| Existing SE baseline | All original DLSS-G retention, FG eligibility, shadow policy, measurement, vanity-camera, settings JSON and shader-scheduling tests passed unchanged. |
| Public interfaces and discovery | Service contracts/registry/foundation/thread affinity, dispatch, startup discovery ordering, mono profile policy and accepted-draw registration/publication passed. |
| Profiling | Actual `Profiler.cpp`, `SEFrameTiming.cpp` and `RenderTargetStats.cpp` compiled against deterministic D3D query fixtures; timing/capture tests passed. |
| Settings and lifecycle | Runtime toggles, world-load policy, shader enablement, global action guards, crop migration, Adaptive Balance/DOF/water settings and measurement restoration passed. |
| Grass, shadows and streaming | Batch/pass caches, history/telemetry, actor bounds/culling contracts, conservative shadow admission, sun receiver and streaming/inventory policy tests passed. |
| Mono upscaling and NR | Temporal tuning, shared guides, sharpening, actor/crop/native layouts, strict settings, color/exposure, memory/recovery, compression and exact prewarm policies passed. Compression includes 79,392 inversion/bounds cases; the exposure fixture includes 548 checks. |
| Screenshots | Production settings/dispatch, mono policies, frozen manifests and nine offline verifier cases passed. |
| Diagnostics | Actual bounded stutter/asset recorder, disabled-build gate, texture COM-sentinel/status/command fixtures, SceneInventory and optional NvPerf policy checks passed. |
| Shader cache and AIO | Managed pack corpus/publication, actual CMake archive create/extract and SE macro/inventory/compatibility packaging passed. Package tests preserve HDR/FG fixtures byte-for-byte. |
| Runtime delivery | Actual Streamline module tests, hash/cache/early-install policy and generated shader deployment with spaces in every path passed. |
| Feature/package metadata | Feature issue scanner, legacy alias admission and seven NR package guard cases passed. |
| FidelityFX C ABI | Compile-time size, alignment and offset assertions passed for 35 x64 descriptors recorded from the SE parent. |

Python cases within the suite exercise real local CMake archive and deployment
operations against isolated fixtures. Synthetic DXBC fixtures validate the pack
parser; they are never represented as compiled game shaders or release payloads.

## Environment and exact aggregate commands

The run used GNU C++ 13.3.0, C++23, CMake/CTest 4.4.4 and Python 3.12 on Linux.
Project test targets use `-Wall -Wextra -Werror -UNDEBUG`. Dependencies were
real nlohmann JSON 3.12.0 headers, Catch2 3.8.1 and the repository-pinned
hlslkit parser/tool dependencies. No compiler, Catch2 or JSON implementation
was replaced with a test stub.

Commands executed from the `main-SE` worktree:

```sh
/tmp/mesh-fix-tools/cmake/data/bin/cmake \
  -S tests/portable -B /tmp/main-se-portable-ui-build \
  -DCSX_JSON_INCLUDE=/tmp/mesh-fix-include \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCSX_PORTABLE_SE_BASELINE=ON \
  -DCSX_BASELINE_FETCH_CATCH2=ON \
  -DFETCHCONTENT_SOURCE_DIR_CATCH2=/tmp/main-se-baseline-deps/Catch2-3.8.1
/tmp/mesh-fix-tools/cmake/data/bin/cmake \
  --build /tmp/main-se-portable-ui-build --parallel 4
PYTHONPATH=/tmp/main-se-cache-tool-deps \
  /tmp/mesh-fix-tools/cmake/data/bin/ctest \
  --test-dir /tmp/main-se-portable-ui-build \
  --output-on-failure --parallel 4 \
  --output-junit /tmp/main-se-ui-test-results.xml
```

The harness passes the chosen compiler, JSON include and CMake executable to
Python fixtures. It also prepends CMake's directory to those tests' PATH; an
absolute `ctest` invocation therefore does not silently lose archive checks.

Catch2's optional fetch is pinned to the official 3.8.1 archive SHA-256:
`18b3f70ac80fccc340d8c6ff0f339b2ae64944782f8d2fca2bd705cf47cadb79`.
The source copy already obtained for the baseline run was reused offline.

## UI follow-up checks

The UI audit found that the earlier port had not updated every shared page.
The 2026-11-01 follow-up completed the desktop header/sidebar/footer,
Home/General/Advanced layouts, 17 rendering feature pages, and the
Upscaling/Neural Rendering/Screenshot pages. Horizon Fix now uses the inherited
structured About page. Details are in [main-se-port.md](main-se-port.md).

Additional focused checks, separate from the 84 CTest entries, passed:

| Check | Evidence and limit |
|---|---|
| Rendering feature pages | All 17 extracted production `DrawSettings` bodies compile under GCC C++23 with the actual `SettingsPage` and widget declarations, settings structs, ImGui and fmt headers. Their 44 section IDs/order match the source branch. Engine boundaries are declared fixtures. |
| Upscaling, NR and Screenshot UI | Changed production UI bodies compile with `-Wall -Wextra -Werror`, genuine ImGui and actual mono policy/runtime/snapshot types. Complete NR and Screenshot class declarations are included; native/game backends are declaration-only boundaries. |
| Shared UI execution | 231 headless frames run against the actual ImGui 1.92.6 docking implementation, including 216 header/page/footer scenarios across three widths, three font sizes, icon/text actions, centered headers and saved/dirty/error-blocked states. Four docked-title-bar frames verify action drawing. The production feature toggle responds to mouse and actual Tab/Space input and preserves disabled/null behavior. Geometry repair and SE font/cursor/palette theme snapshots pass. The fixture records 554,765 assertions, including individual draw-geometry finiteness checks, with no ImGui scope failures. |
| NR hotkey and capture feedback | 154 assertions execute the production toggle, mutation-blocking, screenshot receipt and sequence-polling bodies. Coverage includes requested/active FG ownership, save/load/capture/measurement locks, provider/device/recovery rejection, baseline failure, history/dirty updates, disabling after lost eligibility, and terminal/transient sequence states. |
| Backend preservation | 17 Upscaling/NR/Screenshot backend, serialization and resource functions are byte-for-byte unchanged by the UI follow-up. The 17 rendering page changes preserve stored defaults and rendering algorithms. |
| Build discovery | The existing `CONFIGURE_DEPENDS` C++ source glob includes the new `SettingsFooter.cpp` and header. |

The UI checks use the project's pinned **ImGui 1.92.6 docking** and **fmt 12.1.0**.
The fmt version was read from the manifest's actual vcpkg baseline
`dddca6fa87f177e0678e2545c4b4636a44aa05bd`. Official source archive SHA-256 values:

- ImGui: `5e84cdaa6a6041586a0d11a3071b749734a0439d66fdbdad37ae5b27e37d396c`.
- fmt: `ea7de4299689e12b6dddd392f9896f08fb0777ac7168897a244a6d6085043fea`.

Temporary evidence and reproduction entry points for this work session:

| Evidence | Location |
|---|---|
| Fresh portable build log | `/tmp/main-se-ui-build.log` |
| Final incremental build log | `/tmp/main-se-ui-build-final.log` |
| Aggregate test log / JUnit | `/tmp/main-se-ui-ctest.log`, `/tmp/main-se-ui-test-results.xml` |
| 17 rendering page compiler results | `/tmp/main-se-feature-ui-syntax/results.json` |
| Rendering page syntax generator | `/tmp/check_feature_ui_syntax.py` |
| Upscaling/NR/capture syntax and flow checks | `/tmp/main-se-owned-ui-syntax.py`, `/tmp/main-se-owned-ui-behavior.py` |
| Shared UI/headless fixture | `/tmp/build_main_se_ui_fixture.py`, `/tmp/main-se-ui-headless/` |
| Native configure failure | `/tmp/main-se-native-buildcheck.log` |

These checks found and corrected a duplicate header declaration, unsafe
translated-text format calls, a malformed checkbox call, duplicate header
enable controls and an enable path that could reset saved tuning. They do not
establish that the complete Windows plugin compiles or that the menu has been
rendered in Skyrim.

## Shader and data-layout checks

These checks passed separately from the aggregate CTest run:

| Check | Observed result |
|---|---|
| Rendering shader preprocessing | 398 / 398 flat permutations; no remaining helper-arity findings in expanded source |
| Grass/depth/contact preprocessing | 30 grass permutations plus four helper entry cases |
| NR shader include/preprocessor closure | Nine shader entry variants |
| Shared feature buffer | 25 records, 2,112 bytes, 353 CPU/HLSL field matches |
| Shared frame data | 720 bytes, 31 field matches |
| SSGI constants | 224 bytes, 29 field matches |
| Shadow-copy producer/consumer | 752-byte stride, 12 field matches |
| Screen-space shadow raymarch constants | All 12 shader fields match the used 80-byte prefix of the 96-byte SE CPU allocation |
| Native volumetric blur data | 32-byte layout |
| Managed cache inventory | 3,425 preserved captured tasks -> 4,182 unique tasks, including all 90 HDR tasks |
| Mono NR DevBench control | Strict C++23 syntax fixture using actual settings/policy headers and deterministic platform boundaries |
| Fast-math inputs | Production crop and mono NR settings checks also passed with `-O2 -ffast-math` |

Shader preprocessing checks resolve includes and permutations. They do not
compile HLSL to DXBC or prove GPU behavior. CPU/HLSL packing checks do not
replace Windows ABI compilation and actual shader reflection.

## Metadata and source consistency

Final source/metadata checks passed:

- English extraction: **4,302** inline defaults, in sync with source after the UI follow-up.
- No orphaned English keys; all **eight** other locale files are valid JSON,
  correctly ordered and free of unknown keys or named-placeholder mismatches.
- Existing live translations were preserved. Exact, unambiguous existing
  translations were reused for applicable new keys; obsolete keys were removed.
- The checked-in feature registry matches all **46** feature INIs/Core markers.
  **31** applicable INI versions were reconciled without lowering higher SE
  versions. Project version remains `3.18-SE`.
- Changed Python, JSON and workflow YAML files parse successfully.
- `git diff --check` and an explicit whitespace check of new text files passed.
- Local quoted includes resolve to source, generated headers or declared
  dependencies. A duplicate out-of-line-definition scan found no remaining
  duplicate in the examined source files.
- Source inspection found no added active OpenVR/OpenComposite/stereo/headset
  path. Reserved historical public ABI values remain documented compatibility
  fields and do not advertise implemented VR capabilities.

The integration review also corrected defects found outside pure policy tests:
a duplicate directional-shadow definition; texture demand captured at the wrong
bias/only under upscaling; accepted-draw shared-target publication; shadow-copy
resource and compute-state ownership; early API discovery; post-load reset
consumption; global settings/cache action guards; runtime install ordering;
missing Streamline packaging and CMake deployment argument quoting.

## Dependency state

| Dependency | Worktree commit |
|---|---|
| CommonLibSSE-NG | `48adba4d57a1b3800c13d8abf38d3ffeb0fb74db` |
| FidelityFX SDK DX11 | `995f6a1b5d2670df48c24748bca1f9fd11922fdd` |
| Streamline / SE DX12 headers | `625db07d4bca5abc67d781c42af7e7fb0761f1b6` (`v2.14.16`) |
| NVAPI | `9b181ea572f680327fe01a14a0f1f41c78034104` |

The runtime delivery policy separately pins FidelityFX runtime artifacts from
SDK commit `7539ae8b6568ad29ed78a753404351daf3b5cca7` and the Streamline 2.14.16
archive with explicit file/archive hashes. Native DX11 and DX12 FG payloads
remain separate. The optional NR provider is not redistributed.

## Not run / remaining native gates

The actual project configure command was attempted here:

```sh
/tmp/mesh-fix-tools/cmake/data/bin/cmake --preset ALL \
  -B /tmp/main-se-native-buildcheck \
  -DAUTO_PLUGIN_DEPLOYMENT=OFF -DSKIP_RUNTIME_DOWNLOADS=ON \
  -DZIP_TO_DIST=OFF -DAIO_ZIP_TO_DIST=OFF
```

It exited with code 1 before native compilation:

```text
CMake Error: Could not create named generator Visual Studio 18 2026
```

This is an environment/toolchain block, not evidence that the native C++ sources
compiled or failed compilation. No Visual Studio/MSVC, Windows SDK, MSBuild,
Windows cross-compiler or native shader compiler is installed in this environment.

| Gate | Status |
|---|---|
| Full Windows/MSVC SE/AE plugin build | Configure attempted and blocked by unavailable Visual Studio generator; no plugin compilation or DLL |
| Ordinary and DevBench plugin linkage/startup | Not run |
| FXC/DXC bytecode compilation and reflection | Not run |
| Windows D3D11 WARP tests | Native state-restoration/timing targets are wired, but not run |
| Real NVIDIA/AMD provider execution, NR models and shared GPU fences | Not run |
| In-game SE/AE native hooks, rendering and resource lifetime | Not run |
| HDR/FG presentation, visual quality and measured performance | Not run |
| Native in-game menu rendering, input and accessibility | Not run; focused headless UI checks do not replace this |
| Windows screenshot file/encoder integration | Not run |
| Live package install/deploy into a game | Not run; deployment tests used isolated local fixtures |

NR/FG concurrent execution is intentionally unsupported, and strict HDR
reference/burst capture is explicitly unsupported while ordinary HDR screenshots
and timed sequences are preserved.
The VR-address-only cull-pool patch and global texture-list/FaceGen assignment
hooks were not guessed for SE. Native validation should follow the matrix in
[main-se-port.md](main-se-port.md) and the focused runtime/texture guides.
