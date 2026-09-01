# GPU-unified presets

The three `CSX Unified` MGO presets use one settings file per quality tier on
both AMD and NVIDIA. They do not select a vendor-specific preset directory.

The capability boundary is the existing pair of upscaling settings. Together
they describe one preference, not a staged DLSS-to-FSR transition:

-   `upscaleMethod=3` requests DLSS when Streamline reports DLSS available;
-   `upscaleMethodNoDLSS=2` selects FSR when DLSS is unavailable.

When the active adapter is known to be non-NVIDIA, CSX resolves the preference
directly to FSR without publishing DLSS as the runtime target. On NVIDIA, CSX
waits for Streamline's capability result before publishing either DLSS or the
fallback. An unknown adapter remains unresolved rather than guessing.

Provider-specific tuning remains in the same JSON file. DLSS reads its preset
and sharpener values, while FSR reads its own sharpness and runtime-provider
settings. Shader-quality policy is otherwise shared.

[`unified-preset-policy.json`](./unified-preset-policy.json) is the
machine-readable source. [`generate-unified-presets.ps1`](../../tools/generate-unified-presets.ps1)
pins each inherited template by SHA-256, applies the shared policy and current
settings-schema defaults, rejects vendor names in output directories, and
checks required provider-selection values.

Run the non-writing validation from the repository root:

```powershell
pwsh -NoProfile -File tools/generate-unified-presets.ps1 -Check
```

The generated candidates are intentionally marked provisional. The same
Quality file has passed a live AMD fallback smoke test: DLSS was unavailable,
FSR4 was selected, both eyes were valid, and the render-scale controller
stabilized without lifecycle or fidelity failures. Static policy and schema
validation covers the NVIDIA selection path, but release qualification still
requires a native NVIDIA run proving that Streamline selects DLSS and presents
both eyes correctly.
