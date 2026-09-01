[CmdletBinding()]
param(
    [string] $PythonExecutable
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$schemaRoot = Join-Path $repoRoot 'docs/development/render-map/schemas'

function Assert-True {
    param(
        [Parameter(Mandatory)][bool] $Condition,
        [Parameter(Mandatory)][string] $Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Resolve-Python3 {
    param([string] $RequestedPath)

    $candidates = [System.Collections.Generic.List[string]]::new()
    if ($RequestedPath) {
        $candidates.Add($RequestedPath)
    }
    foreach ($variableName in @('CSX_PYTHON', 'CODEX_PYTHON')) {
        $value = [Environment]::GetEnvironmentVariable($variableName, 'Process')
        if ($value) {
            $candidates.Add($value)
        }
    }
    foreach ($commandName in @('python.exe', 'python3.exe')) {
        $command = Get-Command $commandName -CommandType Application -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($command) {
            $candidates.Add($command.Source)
        }
    }

    foreach ($candidate in $candidates) {
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            continue
        }
        $probe = & $candidate -c 'import sys; print("CSX_PY3" if sys.version_info.major == 3 else "")' 2>$null
        if ($LASTEXITCODE -eq 0 -and ($probe | Select-Object -Last 1) -eq 'CSX_PY3') {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw 'A working Python 3 interpreter is required. Supply -PythonExecutable.'
}

$eventSchemaPath = Join-Path $schemaRoot 'render-event.schema.json'
$graphSchemaPath = Join-Path $schemaRoot 'render-graph.schema.json'
$eventSchema = Get-Content -Raw -LiteralPath $eventSchemaPath | ConvertFrom-Json -Depth 100
$null = Get-Content -Raw -LiteralPath $graphSchemaPath | ConvertFrom-Json -Depth 100

$eventKinds = @($eventSchema.allOf | ForEach-Object {
    $_.if.properties.type.const
})
foreach ($requiredKind in @(
    'command-recording-observed',
    'command-list-observed',
    'finish-command-list',
    'execute-command-list'
)) {
    Assert-True ($eventKinds -contains $requiredKind) "Render-event schema is missing $requiredKind"
}

$hooksSource = Get-Content -Raw -LiteralPath (Join-Path $repoRoot 'src/Hooks.cpp')
$contextHooksSource = Get-Content -Raw -LiteralPath (Join-Path $repoRoot 'src/RenderMap/D3DContextHooks.cpp')
Assert-True ($hooksSource.Contains('stl::detour_vfunc<27, ID3D11Device_CreateDeferredContext>')) 'CreateDeferredContext is not hooked at D3D11 device slot 27'
Assert-True ($contextHooksSource.Contains('stl::detour_vfunc<58, ID3D11DeviceContext_ExecuteCommandList>')) 'ExecuteCommandList is not hooked at context slot 58'
Assert-True ($contextHooksSource.Contains('stl::detour_vfunc<114, ID3D11DeviceContext_FinishCommandList>')) 'FinishCommandList is not hooked at context slot 114'

$python = Resolve-Python3 -RequestedPath $PythonExecutable
& $python -m py_compile (Join-Path $repoRoot 'tools/build-render-graph.py')
if ($LASTEXITCODE -ne 0) {
    throw 'Render-graph builder failed Python compilation.'
}
& $python (Join-Path $repoRoot 'tests/render_graph_builder_test.py')
if ($LASTEXITCODE -ne 0) {
    throw 'Render-graph builder regression suite failed.'
}

Write-Output 'Render-map contracts passed: 2 schemas, 4 command-list events, 3 hook slots, and the offline graph suite.'
