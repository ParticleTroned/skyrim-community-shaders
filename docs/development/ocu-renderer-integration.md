# OCU renderer integration

CSX supports two independent optional interfaces in the same plugin:

| Interface               | Provider    | Consumer            | Purpose                                                                        |
| ----------------------- | ----------- | ------------------- | ------------------------------------------------------------------------------ |
| Accepted scene draws v1 | CSX         | OCU's SKSE plugin   | Observe and replay submitted geometry for DAPA ownership masks.                |
| Effect foveation v1     | OCU runtime | CSX Screen Space GI | Read current per-eye gaze centers and foveation rings for peripheral sampling. |

These interfaces have separate version numbers because they describe
different data and lifetimes. A CSX release number or DLL hash is not part
of either runtime negotiation. Keep the v1 contracts when implementing
compatible updates. Package hashes identify build artifacts, not permitted
runtime versions.

## Accepted scene draws

See [accepted-draw-api.md](accepted-draw-api.md) and
`include/VRAPI/CSacceptedDrawapi.h`. The provider is available only in Skyrim
VR and reports readiness after its geometry and indexed-draw hooks exist.
Consumers can use the existing service registry or the compatible
`CSX_GetAcceptedDrawAPI` export. OCU selects this route before installing
its legacy masking hooks.

The callback follows original command submission with live geometry and
pipeline state. It allows isolated replay into the consumer's own mask
target. It does not classify ownership, implement DAPA, change frame timing,
or provide a pre-draw shader-rate control. Suppressed menu captures and
draws to other depth targets are excluded.

## Effect foveation

`include/OCUEffectFoveationAPI.h` defines the independent 128-byte v1
snapshot. CSX resolves `OCU_GetEffectFoveationV1` from the already loaded
`openvr_api.dll`, or the system-wide `vrclient_x64.dll` provider. It never
loads another runtime. OCU supplies a coherent snapshot for the current
game frame; CSX copies it once and uses it for both eyes.

The client validates the interface, shape, values and publication freshness.
Absent, disabled, unsupported or stale profiles use native sampling.
Source eye-sample timestamps are optional and are not a validity gate.
Fresh profiles recover on the next read without a cooldown. DAPA replay
does not publish another gaze frame or reset CSX's temporal history.

The Screen Space GI setting `ExperimentalOCUEffectFoveation` defaults to
false. Enable it through **OCU peripheral sampling (experimental)**. With
the temporal denoiser and a moving-eye profile, peripheral sampling retains
native ray-step positions and rotates a reduced subset of native slices.
Counts are quantized; three native slices reduce to two in the outer region.
The center retains the native sample count. Stereo Sync and Stereo
Reprojection currently keep native sampling.

The optional compute shader uses a 32-byte constant buffer at b10, restoring
the previous binding, including D3D11.1 offsets. The shader helper and GI
wrapper carry an internal version-2 marker to reject mixed shader files.
This internal shader version is independent of the external v1 API.
Optional variants compile independently after the native shader batch is
ready. A missing, incompatible or failed optional variant keeps native
sampling available, clears the previous optional permutation and does not
trigger a per-frame compilation retry. Clear the shader cache to retry it.

This feature changes the CSX AO/GI sample budget. It does not change
render resolution, vendor upscaler dispatch parameters, or DAPA settings.
NVIDIA hardware VRS may operate alongside it. The effect itself is vendor
independent; AMD headset performance still requires measurement. ENB keeps
the existing CSX startup-disable behavior, so this consumer is unavailable
when CSX is disabled by ENB.

## Updates and testing

Ship one CSX DLL containing both implementations with its matching shaders.
Replacing it with a build lacking an implementation removes that feature.
An interface cannot add integration code to an unrelated build.

Controller test targets are `accepted_draw_registry_test`,
`ocu_effect_foveation_client_test`, `ocu_effect_foveation_policy_test`, and
`ocu_effect_integration_test`. The integration test compiles the production
shader batch and setting handler against controlled dependencies. It
checks independent optional failures, stale permutation removal, required
batch atomicity, 96 setting combinations, and DevBench validation.
`api_service_registry_test` checks the shared registry. The optional
DevBench profiler action `accepted_draws` reports provider readiness and
replay/fault counters. The `ocu_foveation` action reports requested and active
sampling plus its fallback reason without changing settings or starting a
capture. Both actions are advertised in the tool and inspect descriptors.
Use `communityshaders.profiler` with
`{"action":"set_ocu_foveation","enabled":true}` (or `false`) to stage the
setting through the same transition as the UI. It requires a JSON boolean
and loaded SSGI in Skyrim VR. A changed value queues shader compilation
and a history reset for the next SSGI pass. Repeating the current value
does neither. Settings are not written to disk by this action.
The existing feature API settings inspection includes
`ExperimentalOCUEffectFoveation`; the GI settings UI reports runtime status.

In the same Skyrim session, confirm the CSX provider-ready log, OCU's
accepted-draw registration log, and active peripheral sampling in the GI UI.
Exercise gaze loss/recovery, DAPA on/off, menus, hands/body/held objects,
weapons/arrows, render-scale changes and each selected vendor upscaler.
Capture real frame times and compare the same scene. Communication and
shader tests do not establish live draw coverage or compositor smoothness.
