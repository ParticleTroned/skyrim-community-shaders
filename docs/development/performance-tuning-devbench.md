# Performance-tuning DevBench automation

The communityshaders.performance_tuning tool runs closed-menu cost
measurements and returns their progress, results, and raw timing diagnostics.
The action is nonblocking: start it once, then poll status.
Status is a read-only preflight: its readiness and capabilities objects expose
only the in-game VR, idle, menu, measurement, cooldown, adapter, and runtime
facts required to start. It does not prepare a cell or alter FOV, TAA,
foveation, developer mode, logging, or debug settings.

## Upscaling sweep

Start with matrix set to auto, nvidia, or amd. Auto selects from the active
adapter and runtime capabilities.

On NVIDIA, a start without dlssPreset does not change runtime state. It returns
promptRequired and lists every supported profile:

    J, K, L, M, F, E

Repeat the start action with the selected letter. That one profile is used for
every DLSS and DLAA case. For example:

    {
      "action": "start_upscaling_sweep",
      "matrix": "nvidia",
      "dlssPreset": "K"
    }

The NVIDIA matrix contains TAA, seven DLSS/DLAA quality cases, and seven FSR3
quality cases. The AMD matrix requires an AMD adapter with an available FSR4
runtime and contains TAA plus seven FSR3 and seven FSR4 quality cases. Native
AA/DLAA runs without render scale; quality modes 1 through 6 run with their
corresponding render scale. A latched or unsupported FSR provider fails closed
instead of reporting its FSR3 fallback as FSR4. Every case is independently
compared with None.

Each case uses a five-second cooldown after CS closes, five one-second target
windows, a nine-second wait after switching to None, five one-second None
windows, and exact case restoration. A ten-second cooldown separates cases.
The original Upscaling state is restored after completion, cancellation, or
failure. CS reopens only when it was open before the sweep.

## Timing diagnostics

Poll a bounded trace page while a measurement is running:

    {
      "action": "status",
      "traceAfterSequence": 0,
      "maximumTraceSamples": 128
    }

Use the returned trace.nextAfterSequence as the next cursor. Samples are raw
Game, GPU, and CPU frame times captured at 100 ms intervals. Each sample
identifies the case, phase, run elapsed time, and phase elapsed time, including
initial cooldown, None wait, measurement, inter-case cooldown, and restoration.

Use an action of cancel to stop a DevBench-owned sweep and restore the original
state.
