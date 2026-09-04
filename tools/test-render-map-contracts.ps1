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
$fixtureRoot = Join-Path $repoRoot 'tests/fixtures/render-map'
$fixtureEventsPath = Join-Path $fixtureRoot 'deferred-command-events.json'
$fixtureEdgeCasesPath = Join-Path $fixtureRoot 'deferred-command-edge-cases.json'
$fixtureIdentityCasesPath = Join-Path $fixtureRoot 'deferred-command-identity-cases.json'
$fixtureManifestPath = Join-Path $fixtureRoot 'deferred-command-capture-manifest.json'
$fixtureEvents = Get-Content -Raw -LiteralPath $fixtureEventsPath | ConvertFrom-Json -Depth 100
$fixtureEdgeCases = Get-Content -Raw -LiteralPath $fixtureEdgeCasesPath | ConvertFrom-Json -Depth 100
$fixtureIdentityCases = Get-Content -Raw -LiteralPath $fixtureIdentityCasesPath | ConvertFrom-Json -Depth 100

Assert-True (@($fixtureIdentityCases.contexts.PSObject.Properties).Count -ge 2) 'Identity fixture requires at least two contexts'
Assert-True (@($fixtureIdentityCases.recordings.PSObject.Properties).Count -ge 2) 'Identity fixture requires at least two recordings'
Assert-True (@($fixtureIdentityCases.commandLists.PSObject.Properties).Count -ge 2) 'Identity fixture requires at least two command lists'
Assert-True (@($fixtureIdentityCases.contradictions).Count -eq 13) 'Identity fixture must enumerate all ownership contradiction cases'

function New-EdgeCaseEvent {
    param(
        [Parameter(Mandatory)] $Case,
        [Parameter(Mandatory)][int] $Sequence
    )

    return [ordered]@{
        schema = [ordered]@{ name = 'csx.render-event'; major = 1; minor = 17; producerVersion = 'contract-test' }
        captureId = 'capture-deferred-command-contract'
        sequence = $Sequence
        timestampQpc = 2000 + $Sequence
        processId = 1
        threadId = 2
        frame = [ordered]@{ cpuFrame = 1; sceneEpoch = 1; submissionEpoch = $null; eye = 'unknown'; eyeMask = $null }
        execution = [ordered]@{
            observationDomain = $Case.observationDomain
            commandStreamSequence = $Sequence
            gpuTimestampTicks = $null
            gpuTimestampFrequencyHz = $null
        }
        deviceContextObservationId = $Case.deviceContextObservationId
        commandRecordingObservationId = $Case.commandRecordingObservationId
        submissionObservationId = $null
        type = $Case.type
        scopes = [ordered]@{ renderPass = $null; technique = $null; geometry = $null; commandList = $null }
        causes = @()
        manifestRefs = @()
        engineRefs = @()
        observationRefs = @()
        payload = $Case.payload
        extensions = [ordered]@{ 'csx.contractCase' = $Case.name }
    }
}

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

foreach ($event in $fixtureEvents) {
    $eventJson = $event | ConvertTo-Json -Depth 100 -Compress
    $eventValid = Test-Json -Json $eventJson -SchemaFile $eventSchemaPath -ErrorAction Stop
    Assert-True $eventValid "Deferred-command fixture event $($event.sequence) does not conform to the render-event schema"
}

$validEdgeCaseEvents = @()
$edgeCaseSequence = 100
foreach ($case in $fixtureEdgeCases.valid) {
    $event = New-EdgeCaseEvent -Case $case -Sequence $edgeCaseSequence
    $eventJson = $event | ConvertTo-Json -Depth 100 -Compress
    $eventValid = Test-Json -Json $eventJson -SchemaFile $eventSchemaPath -ErrorAction Stop
    Assert-True $eventValid "Valid deferred-command edge case '$($case.name)' does not conform to the render-event schema"
    $validEdgeCaseEvents += $event
    $edgeCaseSequence++
}
foreach ($case in $fixtureEdgeCases.invalid) {
    $event = New-EdgeCaseEvent -Case $case -Sequence $edgeCaseSequence
    $eventJson = $event | ConvertTo-Json -Depth 100 -Compress
    $eventValid = Test-Json -Json $eventJson -SchemaFile $eventSchemaPath -ErrorAction SilentlyContinue
    Assert-True (-not $eventValid) "Invalid deferred-command edge case '$($case.name)' unexpectedly conforms to the render-event schema"
    $edgeCaseSequence++
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

$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("csx-render-map-contract-" + [guid]::NewGuid().ToString('N'))
try {
    $null = New-Item -ItemType Directory -Path $temporaryRoot -Force
    $eventsJsonlPath = Join-Path $temporaryRoot 'events.jsonl'
    $graphOutputPath = Join-Path $temporaryRoot 'render-graph.json'
    @($fixtureEvents) + @($validEdgeCaseEvents) |
        ForEach-Object { $_ | ConvertTo-Json -Depth 100 -Compress } |
        Set-Content -LiteralPath $eventsJsonlPath -Encoding utf8
    $graphArguments = @(
        (Join-Path $repoRoot 'tools/build-render-graph.py'),
        '--capture-manifest', $fixtureManifestPath,
        '--events', $eventsJsonlPath,
        '--output', $graphOutputPath
    )
    & $python @graphArguments
    if ($LASTEXITCODE -ne 0) {
        throw 'Deferred-command schema fixture failed graph generation.'
    }
    $graphValid = Test-Json -LiteralPath $graphOutputPath -SchemaFile $graphSchemaPath -ErrorAction Stop
    Assert-True $graphValid 'Generated deferred-command graph does not conform to the render-graph schema'
} finally {
    if (Test-Path -LiteralPath $temporaryRoot) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
}

Write-Output "Render-map contracts passed: 2 schemas, $($fixtureEvents.Count) baseline deferred-command fixtures, $($validEdgeCaseEvents.Count) valid edge cases, $($fixtureEdgeCases.invalid.Count) rejected edge cases, 13 cross-identity cases, 3 hook slots, and the offline graph suite."
