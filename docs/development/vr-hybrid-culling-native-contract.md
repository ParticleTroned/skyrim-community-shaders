# Skyrim VR hybrid culling native contract

This record separates verified Skyrim VR 1.4.15 integration contracts from
remaining runtime validation. It supports the experimental Hi-Z backend;
it is not performance or visual acceptance evidence.

## Evidence identity

The inspection used read-only `ReadProcessMemory` against the existing
`SkyrimVR.exe` process, PID 7284, started on 2026-09-25 at 22:46:08 local
time. Each script verified the process executable path before following
only the culler, camera, and native shader records needed for this work.
No process code, state, or GPU resources were modified. Capstone 5.0.6
decoded bounded x64 code windows; the system `D3DDisassemble` decoded the
two native vertex shader bytecodes.

-   Executable: `C:/Program Files (x86)/Steam/steamapps/common/SkyrimVR/SkyrimVR.exe`
-   File version: `1.4.15.0`
-   Executable SHA-256: `6961efb4f4775a307b0fc9a3d637542c1e090be207d3b09467eab216b7f87971`
-   Module base during capture: `0x7ff6e9c60000`
-   Implementation checkout base: `df9f377a53d237518c1b671b7be1085c9a65b69d`
-   Local evidence: `build/native-contract/` in the working checkout.

The running process already contained CSX callsite detours. Native bodies
and surrounding instructions were inspected directly; a detoured call
target was not treated as an unmodified executable signature. Addresses
below are module RVAs, not process absolute addresses.

## Producer and object association

The object passed to the existing render hook is the native
`BSOBBOcclusionTestingShader` culler itself. The existing
`BSImagespaceShader*` parameter spelling does not describe its full class
layout. The culler extends the 0x90-byte `BSShader` base and has native
vtable RVA `0x1908500`. Its constructor is at `0x1355770`.

Both the render callsite `0x1323601` and readback callsite `0x132208B`
receive the singleton stored at `0x36F1870`. The render entry is
`0x13561F0`; readback is `0x1356270`.

| Culler field     | Verified meaning                                    |
| ---------------- | --------------------------------------------------- |
| `+0xB0`          | Current registration count, maximum 4096            |
| `+0xB8`          | CPU affine OBB array, 64 bytes per entry            |
| `+0xC0`          | CPU result-array selector, 0 or 1                   |
| `+0xC9`          | OBB upload-completed flag                           |
| `+0xD0`, `+0xD8` | CPU `uint32_t[4096]` result arrays                  |
| `+0xF8`          | Structured OBB buffer wrapper                       |
| `+0x100`         | Structured result buffer wrapper, including staging |
| `+0x108`         | GPU zero-source buffer wrapper                      |

Registration at `0x13560A0` atomically allocates index `i` from `+0xB0`,
writes the OBB at `transforms[i]`, initializes `results[selector][i]` to
zero, and returns the address of that result element to the native
caller. The index establishes association for that submission only.
It does not establish persistent object identity across frames.

Upload at `0x13562D0` copies `count * 64` bytes from the CPU array into
the native dynamic OBB buffer, then sets `+0xC9`. The observed callers
are `0x13220FE` after collection and `0x132336A` before depth rendering.
The producer method itself does not perform this upload.

Readback uses the current count and selector. Only after it returns does
the caller at `0x1322090` set the next selector from the frame parity,
reset `+0xB0`/`+0xB4`, and clear `+0xC9`. Therefore, a producer snapshot
can be checked against the same batch at the readback hook before native
re-registration. Check the culler, count, selector, CPU result address,
transform address/content, and rendering epoch. Do not attach temporal
hysteresis to index `i` across different submissions.

## GPU buffers and deferred delivery

The native allocator at `0xDC0CA0` creates 0x30-byte buffer wrappers:

| Wrapper field | Verified type                                    |
| ------------- | ------------------------------------------------ |
| `+0x00`       | `ID3D11Buffer*` GPU resource                     |
| `+0x08`       | `ID3D11ShaderResourceView*`                      |
| `+0x10`       | `ID3D11UnorderedAccessView*`, when requested     |
| `+0x18`       | `ID3D11Buffer*` staging resource, when requested |
| `+0x28`       | Element capacity                                 |

The allocator sets `D3D11_RESOURCE_MISC_BUFFER_STRUCTURED` and the
requested structure stride. It creates SRVs/UAVs with default view
descriptions. The OBB buffer has stride 64 and capacity 4096; the result
buffer has stride 4 and capacity 4096. This is a structured `uint`
result UAV, not a typed `R32_UINT` view. The native vertex shader also
declares its OBB input as a structured resource with stride 64.

The native end-pass routine calls `0xDC0F30`, which copies the result GPU
buffer into that wrapper's staging buffer. On the following consumption
path, `0xDC11B0` maps staging with `D3D11_MAP_READ`, copies `count * 4`
bytes to the selected CPU array, and unmaps it. The subsequent
`0xDC0F80` call resets the GPU result resource by copying the zero-source
buffer at culler `+0x108`.

A replacement can dispatch into the existing result UAV, unbind it, and
copy the GPU result buffer to its existing staging buffer. This preserves
the native result addresses and delivery lifecycle. Every active result
must be written. The map uses flags zero: delivery is deferred, but the
native map can still wait if its copy has not completed. No nonblocking
readback guarantee is implied.

Validate resource and view descriptions before using these offsets:
buffer dimensions and byte widths, structured strides and capacity,
required bind flags, staging usage and CPU read access, and matching
resources behind the supplied views. Preserve the engine's bindings and
resource ownership; the wrappers remain native-owned.

## Depth preparation and replacement boundary

The outer downscale routine is `0x1322D80`, called at `0x13235CF`.
It prepares depth target 14 (`kMAIN_DOWNSAMPLE`) and calls the imagespace
dispatcher at `0x1322EC2` with effect 100
(`ISCopyDepthBufferTargetSize`), source depth target 7
(`kPOST_ZPREPASS_COPY`), and the depth-texture flag set. The texture
constructor at `0x133F8D0` confirms that the source argument is a depth
target index. CSX's Terrain Blending hook can alias this target's depth
SRV; using the same effective SRV preserves that source selection.

The outer routine also snapshots both world-camera views and positions
and writes the depth-ready byte at `0x1ED4180`. The caller checks that
byte at `0x13235EF` before invoking the culler. Skipping the whole
downscale function would discard these side effects and can suppress
the producer call.

The focused replacement boundary is the inner imagespace call at
`0x1322EC2`. Replacing only this operation preserves the outer state
restoration, camera snapshots, and ready latch. If a later producer
preflight fails after skipping the native downsample, regenerate native
downsample depth under an explicit bypass before running the native
producer. Reusing an older native downsample would be unsafe.

Its effective signature is `void(ImageSpaceManager*, uint32_t effect,
RENDER_TARGET source, RENDER_TARGET destination, ImageSpaceEffectParam*,
bool sourceIsDepth)`. Registers contain manager, 100, 7, and -1;
the stack arguments contain the parameter-object pointer and true.
The called dispatcher is `0x12D23D0`.

The captured camera viewports were full eye rectangles
`{left=0,right=1,top=1,bottom=0}` with depth range `[0,1]`. The sampled
dynamic-resolution ratios were one. Native viewport setup multiplies
target dimensions by the active ratios and truncates to integer pixels
when dynamic-resolution locking does not override them. The new backend
must validate its actual source dimensions and active stereo rectangles;
this single full-resolution observation does not qualify scaled,
cropped, masked, or odd-sized layouts.

## Exact stereo transform convention

Native vertex shader IDs 0 and 1 were extracted from the culler's shader
map. Both load four `float4` rows per OBB, generate signed unit-cube
corners, and subtract the eye's camera position from the translation
elements `row0.w`, `row1.w`, and `row2.w`. They then calculate four row
dot-products against the corner. The result is transformed by the four
rows of the eye's uploaded view-projection matrix.

The native upload transposes `ViewData.viewProjMat` before writing those
rows. Consequently, direct CPU `ViewData.viewProjMat.Transpose()` matches
an HLSL `row_major float4x4` multiplied by a column vector. The cached
per-frame GPU representation already contains that transpose. Mixing
those two representations adds an incorrect extra transpose.

Native stereo rasterization doubles the instance count, chooses eye
`instance & 1`, obtains OBB `instance / 2`, and packs clip X into the
combined stereo target. A compute test against independent eye slices
uses the eye clip coordinates before that final packing. Both eyes must
establish occlusion before an object can be rejected.

The native camera-cache lookup at `0xDD1AD0` has the effective signature
`void*(BSGraphics::State*, NiCamera*, bool useJitter)`. It searches only;
it returns null when the requested entry is absent. Entries have stride
0x70. The downscale path requests the world-root camera with jitter off.

| Camera-cache field | Verified meaning                           |
| ------------------ | ------------------------------------------ |
| `+0x00`            | Referenced `NiCamera*`                     |
| `+0x08`            | `BSTArray<ViewData>`, element stride 0x250 |
| `+0x20`            | `BSTArray<NiPoint3>` position adjustments  |
| `+0x38`, `+0x50`   | Current and previous position arrays       |
| `+0x68`            | Jitter selection                           |
| `+0x6C`            | Camera-data count                          |

Each array has its data pointer at array `+0x00` and size at array
`+0x10`. The world-camera entry observed here had exactly two elements
in all four arrays. Its `+0x20` position values matched the native
culler's two eye adjustments. The renderer shadow-state snapshot's
first position instead contained the eye midpoint, demonstrating why
arbitrary shadow-state samples are not a substitute for an attributed
world-camera stereo snapshot.

The convenience getter at `0xDCF970` indexes the view array but does not
check whether lookup returned null. Prefer the checked cache lookup.
Avoid iterating `CameraStateData` through the cross-runtime CommonLib
class: its ALL-build type does not represent the native VR stride.

Cache availability does not by itself establish cache freshness at
readback. Validate the actual live world-camera transform and the
producer frame/epoch alongside stereo cache data. This inspection did
not establish motion correctness or independently moving-occluder
validity; those remain explicit runtime acceptance requirements.

## Preserved artifact fingerprints

Raw process dumps and native shaders remain local. The decisive bounded
captures are identified below; their adjacent JSON files preserve PID,
module base, exact executable path, length, and SHA-256.

| Capture         | RVA         | Bytes | SHA-256                                                            |
| --------------- | ----------- | ----- | ------------------------------------------------------------------ |
| `readback`      | `0x1356270` | 1536  | `902137ab2ea421e16f94eb4f4f05d8ec9e86af7d886a546ff153495e0d943143` |
| `render`        | `0x1323250` | 1792  | `8a984a3a101862078bd90035297e10979af63eccef09c9cae59bb7f6eebe576f` |
| `consume`       | `0x1321E00` | 1536  | `e3b0349fe3f0878d2b1251d2018ebdf29a753cc6e93237667bc9623ec1fa13d1` |
| `culler`        | `0x1355900` | 2560  | `ff4e503bc850466d3b5566d90372804b7e07ed887b9669444edfb22296e50b52` |
| `buffers`       | `0xDC0CA0`  | 1616  | `e32227c0318dfc471c0a338b1fbc7387b33da71a674bee6b4ea5d453331b84c9` |
| `downscale`     | `0x1322D80` | 1232  | `00f43e5bbb0a47812cecf276b79d6800928e9fc489ae3937db0eacd17c83a11b` |
| `cache_getter`  | `0xDD1AD0`  | 1024  | `6be662df4fc6e3de574b8abdc6e7b708263ac70c1aad28637715d1e3114d004f` |
| `obb-vs-0.dxbc` | Shader ID 0 | 2044  | `ca531f29219dd4705b604ed978443871d69711b4eeccbd81a8ac1c2097bd0800` |
| `obb-vs-1.dxbc` | Shader ID 1 | 2140  | `cd5dbcc602bcee9853875b74c3908ef420ffff946d3913f8c659cb6c282a78a4` |

The task-local script hashes are:

-   `capture.py`: `e36e8fa05dd085ca6d9174b1b6f86cd276d3598d4eb3463c1fea898849f16756`
-   `shader.py`: `cd800f07c3435820d5bfdd6febf6f08bcd908f6014c8e0dbb4718a950296cd63`
-   `camera.py`: `79dee2599d116961808a970719c8321fb77c2e06b710ec607e47fe1cf9c0b1ca`
