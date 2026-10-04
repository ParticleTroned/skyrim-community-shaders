# NR master control and runtime recovery

Cycling the master switch must preserve character, FOV and colour
preferences. Runtime admission remains separate from those preferences:
missing prerequisites do not clear them, and unsafe resources must not be
released or reused merely because a checkbox was clicked.

## Corrected recovery visibility

The NR panel previously placed runtime failure details and its reset
button inside the advanced section displayed while NR was enabled. With
NR disabled and FOV available, this hid the reason for a rejected enable
attempt and the available recovery action.

The master control now takes one coherent renderer snapshot when drawn.
It retains the failure reason and offers a recoverable runtime reset at
normal logging levels, including while NR is off. Enabling is blocked
until a latched failure is explicitly reset. Quarantine retains its
restart requirement; switching an enabled master off remains available.
Successful reset clears runtime and character history without enabling
NR or changing saved preferences. Passive redraw performs no reset.
This adds no work to the normal rendering path when the panel is closed.

## Validation and remaining investigation

The real master helper is extracted into `NeuralRenderingUI`, covering
repeated healthy off/on cycles, preference retention, character edits,
recoverable and failed resets, persistent reasons, quarantine, and
balanced UI scopes. `NeuralRenderingControls` exercises the production
settings-transition handler and its independent acceptance/retirement
outcomes. Both passed in the Release configuration under
`build/nr-ui-compatible-kernels-20261004`; focused pre-commit hooks passed.
The VR DevBench DLL also compiled successfully.

The user additionally reports that all NR panel switches revert after
cycling the master, including colour processing. That broader symptom
has not been reproduced. The preceding live session recorded 83
successful resets, zero reset failures, zero NR failures and zero
quarantines. Hidden recovery feedback therefore does not establish the
cause of that session's reported symptom. A new live UI reproduction is
still required; the source correction and mock/controller passes are
not a qualification of the whole-panel report. No game was restarted or
DLL installed for this offline check.
