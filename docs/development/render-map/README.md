# Render-map runtime

The render-map runtime is a bounded, opt-in diagnostic collector for observing
Community Shaders and Skyrim rendering state. It records render-pass,
technique, geometry, material, shader, D3D11 resource/view/binding, CPU-access,
resource-version, draw/dispatch, culling-observation, and accepted eye-submit
events.

The collector is inert until a controller starts a capture. Capture bounds
limit event count, byte use, string use, and frame duration; truncation and
gaps are represented explicitly. Stopping a capture produces an immutable
completed-capture snapshot which the artifact layer can serialize without
holding render-thread state.

This runtime does not change depth-culling decisions or apply culling results.
Depth-culling events are observations of the existing upstream behaviour.

## Included here

- `Collector`: bounded event and string storage with explicit gap accounting.
- `Runtime`: typed observation entry points used by engine and D3D11 hooks.
- `Controller`: single-session start, status, stop, and completed-capture
  ownership.
- `Artifacts` and `Serialization`: deterministic JSONL event and capture
  manifest output.
- engine, shader, D3D11 context, and OpenVR eye-submit instrumentation.
- focused collector, runtime, and controller tests.
- the runtime capture-manifest and render-event JSON schemas.

## Deliberately separate

The DevBench registration adapter is reviewed separately because it is an
optional external control surface over this runtime. Offline graph building,
shader dependency analysis, generated shader manifests, engine maps, Ghidra
helpers, prior-art catalogues, and captured-analysis reports remain development
work and are not part of this upstream runtime change.
