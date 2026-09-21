# Graphics hook installation

The shared SE and AE graphics hooks check every Detours transaction
step, including thread enlistment. A failed begin never aborts another
transaction or starts a fallback. Failed enlistment or attachment must
successfully abort before a fallback can run. Successful native Detours
installation keeps the existing function-hook behavior.

When Detours cannot install a graphics hook, CSX first tries changing the
virtual-function table entry. It restores the page protection afterward.
If the page refuses that change, CSX queries the known D3D11 or DXGI
interfaces sharing the object's address and copies their complete table.
Unreadable or null entries reject the entire copy. The fallback supports
the published interfaces through ID3D11Device5, ID3D11DeviceContext4, and
IDXGISwapChain4; it is not a generic arbitrary-object hook.

The original function pointer and storage ownership are established
before the hook is published. Pointer replacement checks that its expected
target still exists. Hooks on one cloned table preserve earlier slots;
repeated installation preserves the original call target. Reusing one
hook's static call target for a different implementation is rejected.
Published clones remain allocated until process exit, including when a
graphics object is replaced. No GPU resources or graphics state are added.

Fallback use logs the slot and original Detours error. An unrecoverable
graphics-hook installation or protection-restoration error stops startup
with an explicit diagnostic. This prevents rendering from continuing with
missing frame-buffer tracking or a partially installed correction.

This adapts [upstream #2648](https://github.com/community-shaders/skyrim-community-shaders/pull/2648)
and [the OS thread-update correction](https://github.com/alandtse/open-shaders/commit/3081ce69e2ba9bef2d682a26d01a86d9a45538fd).
The reported CrossOver failure concerns host-mapped D3D implementations
whose code and tables reject protection changes. Native Windows retains
the successful Detours path.

## Validation

With controller tests enabled, `virtual_function_hook_test` exercises the
production installer using injected transaction and protection failures.
It covers transaction ownership, thread-update failure, native-path
idempotence, direct-table and cloned-table hook chains, preserved tail
methods, retained old clones, incomplete reads across a protected page,
invalid slots, changed implementations, protection-restoration failure,
and rejection of a clone when the object's dispatch pointer is read-only.

After building that target, run:

```powershell
ctest --test-dir build/ALL -C Release -R '^VirtualFunctionHook$' --output-on-failure
```

The focused executable compiles the production installer into an isolated
host test. It does not replace live SE/AE or CrossOver runtime validation.
