# Optional FSR temporal reconstruction tuning

The Upscaling menu exposes an optional reconstruction profile for verified
FSR 3.1.4 and 3.1.5 runtime providers. It is disabled by default: existing
configurations issue no new configure calls and retain vendor defaults.
These controls affect temporal reconstruction, separately from sharpening.
They do not select a provider or change render resolution.

Expand **Temporal reconstruction tuning**, edit the controls, and select
**Apply reconstruction tuning**. Edits are applied together so dragging a
slider does not repeatedly recreate contexts. **Restore vendor
reconstruction** disables the profile. The regular settings save includes
the `Upscaling.fsrTemporalTuning` object.

| Setting                           | Initial value | Range | Intended tradeoff                                                    |
| --------------------------------- | ------------: | ----: | -------------------------------------------------------------------- |
| `velocityFactor`                  |             1 |   0–1 | Lower values can stabilize bright pixels in motion.                  |
| `reactivenessScale`               |             1 |   0–4 | Scales the reactive mask's influence.                                |
| `shadingChangeScale`              |             1 |   0–4 | Higher values respond more strongly to shading changes.              |
| `accumulationAddedPerFrame`       |         0.333 |   0–1 | Lower values can reduce ghosting at a cost of thin-detail stability. |
| `minimumDisocclusionAccumulation` |        -0.333 |  -1–1 | Higher values can reduce disocclusion flicker but increase ghosting. |

The response-scale cap of four is a CSX configuration bound; AMD's API
allows a nonnegative unbounded multiplier. The initial values are editable
starting points from AMD's documentation. Disabling creates untouched
contexts instead of writing these values as presumed provider defaults.

## Compatibility and failure behavior

The public FidelityFX configure API is used on SE, AE and VR through the
existing runtime FSR path. Every context must report the same supported
provider ID. The entire five-key set is limited to exact FSR 3.1.4/3.1.5
versions. Host FSR, older or unknown providers, future unverified versions
and FSR4 retain vendor behavior, with an explicit unsupported state. There
is no inference of FSR4 support from FSR3 key availability.

Changing an enabled profile invalidates context compatibility through an
atomic request revision. The existing GPU drain and context recreation
path handles the change. All settings are configured on fresh contexts
before any eye can dispatch. No settings are repeatedly configured on
unchanged frames. Changing disabled slider values causes no recreation.
If an eye already dispatched, the profile change waits until the next frame
so sequential eye submissions retain the same reconstruction profile.

If any configure call fails, no partially configured eye is dispatched.
The complete fresh context set is destroyed and recreated without tuning.
The rejected request is retained with its revision, provider ID and result
code; unchanged requests are not retried each frame. Changing the profile
or provider permits another attempt, including edits away and back before
the next render-thread application. A faulting provider follows the existing
session quarantine and host-fallback policy, retaining indeterminate
resources. Unknown capability is not a successful tuning application.

Malformed, nonfinite or out-of-range live requests leave the prior settings
unchanged. Numeric bounds are checked before conversion to float. Invalid
values or types in a loaded profile disable only the reconstruction profile
and log the fallback; unrelated upscaling settings still load. Unknown live
keys are rejected, while unknown persisted keys remain forward compatible.
Persistence, live patches and the DevBench schema share the numeric field
contract. Settings and diagnostic snapshots use a mutex; unchanged frame
compatibility uses only an atomic revision read.

Every fresh-context provider identity query uses the same protected call.
A query fault marks its exact context indeterminate and returns through the
existing runtime quarantine before configuration or dispatch can proceed.

## DevBench

`communityshaders.fsr_temporal_tuning` provides `status` and `set` actions
without changing the public upscaling-service ABI. `status` is read-only.
`set` atomically patches the current requested profile on the main thread.
It reports acceptance separately from render-thread application:

```json
{
    "action": "set",
    "settings": {
        "enabled": true,
        "velocityFactor": 0.5,
        "reactivenessScale": 1.0
    },
    "persist": false
}
```

`persist` defaults to false. Set it to true to save the normal user
configuration. A failed save reports that the live request was accepted
but was not persisted. `expectedBuildId` can require an exact producer DLL.

Inspect `requested`, `contextSettings`, `status`, `providerId`,
`providerVersionSupported`, `configuredContexts`, `lastConfigureResult`,
`requestRevision`, and `lastDispatchPath`. A host dispatch reports inactive
overrides even if dormant runtime contexts still contain a tuned profile.
These fields describe the latest FSR dispatch and its context configuration;
switching to another upscaler can leave this historical status visible.
The dispatch path uses the existing values: 0 inactive, 1 host FSR3,
2 runtime FSR3, 3 runtime FSR4, and 4 host FSR3 fallback. `pending` is not
proof of application; `rejected_vendor_defaults` records a rejected
request even after default contexts have been restored.

## Validation

`FSRTemporalTuningPolicy` checks bounds (including NaN and infinity), exact
provider guards, disabled-profile compatibility, rejection-latch identity,
and injected failure/fault at each key of each eye. Every injected failure
stops subsequent configure calls and preserves its context, key and error.

`FSRTemporalTuningSerialization` compiles the production profile loader and
checks malformed type isolation, atomic live patches, exact numeric bounds,
unknown keys, older profiles and save/load round trips.
`FSRTemporalTuningProvider` compiles the production protected query and
provider-result handler, injects a Windows structured exception, and checks
context quarantine, failure propagation and retained diagnostic evidence.

Runtime validation must compare fixed scenes and identical camera paths,
including foliage, bright particles, water and disocclusions. Test live
enable/edit/disable, both eyes, provider rejection, FSR4, host fallback,
save/reload and SE/AE/VR independently. Visual quality and performance
improvements remain unmeasured until those checks run. VR render-scale
qualification and evidence reporting follow the repository protocol.

## References and acknowledgment

The design was informed by OptiScaler's configurable FSR reconstruction
constants at commit
[`d36a078fb79a7a36e5dd356e4e173fdd9c7e29ec`](https://github.com/optiscaler/OptiScaler/commit/d36a078fb79a7a36e5dd356e4e173fdd9c7e29ec),
particularly
[`FFXFeature_Dx12.cpp`](https://github.com/optiscaler/OptiScaler/blob/d36a078fb79a7a36e5dd356e4e173fdd9c7e29ec/OptiScaler/upscalers/ffx/FFXFeature_Dx12.cpp#L489)
and
[`FFXFeature.cpp`](https://github.com/optiscaler/OptiScaler/blob/d36a078fb79a7a36e5dd356e4e173fdd9c7e29ec/OptiScaler/upscalers/ffx/FFXFeature.cpp#L153).
OptiScaler is GPL-3.0 licensed. This implementation uses that work as a
design reference; no OptiScaler code or tuning presets were copied or
adapted. Existing project and vendor license notices remain applicable.

The public API, default values and supported numeric ranges come from
[AMD's FSR 3.1.5 constant override documentation](https://gpuopen.com/manuals/fsr_sdk/techniques/super-resolution-upscaler/#constant-overrides)
and the
[FSR 3.1.4 plugin release notes](https://gpuopen.com/learn/ue-fsr3/).
