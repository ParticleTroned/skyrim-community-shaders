[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$renderMapRoot = Join-Path $repoRoot 'docs/development/render-map'
$schemaRoot = Join-Path $renderMapRoot 'schemas'
$exampleRoot = Join-Path $renderMapRoot 'examples'
$renderMapBridgePath = Join-Path $repoRoot 'src/RenderMap/DevBenchBridge.cpp'
$d3dContextHooksPath = Join-Path $repoRoot 'src/RenderMap/D3DContextHooks.cpp'

function Assert-True {
    param(
        [Parameter(Mandatory)]
        [bool] $Condition,
        [Parameter(Mandatory)]
        [string] $Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Assert-Unique {
    param(
        [Parameter(Mandatory)]
        [object[]] $Values,
        [Parameter(Mandatory)]
        [string] $Description
    )

    $duplicates = @($Values | Group-Object | Where-Object Count -gt 1)
    Assert-True ($duplicates.Count -eq 0) "$Description contains duplicate values: $($duplicates.Name -join ', ')"
}

function Assert-JsonSchema {
    param(
        [Parameter(Mandatory)]
        [string] $JsonPath,
        [Parameter(Mandatory)]
        [string] $SchemaPath
    )

    $valid = (Get-Content -Raw -LiteralPath $JsonPath) | Test-Json -SchemaFile $SchemaPath
    Assert-True $valid "$JsonPath does not validate against $SchemaPath"
}

$schemaFiles = @(
    'prior-art-catalog.schema.json',
    'engine-map.schema.json',
    'capture-manifest.schema.json',
    'render-event.schema.json',
    'render-graph.schema.json'
)

foreach ($schemaFile in $schemaFiles) {
    $schemaPath = Join-Path $schemaRoot $schemaFile
    $null = Get-Content -Raw -LiteralPath $schemaPath | ConvertFrom-Json
}

$renderMapBridgeSource = Get-Content -Raw -LiteralPath $renderMapBridgePath
$d3dContextHooksSource = Get-Content -Raw -LiteralPath $d3dContextHooksPath
Assert-True ($renderMapBridgeSource.Contains('"geometryShaderTypes":{"type":"array","minItems":1,"uniqueItems":true,"items":{"type":"integer","minimum":0,"maximum":63}}')) 'Render-map MCP schema does not advertise geometryShaderTypes'
Assert-True ($renderMapBridgeSource.Contains('"executionWithinSelectedGeometry":{"type":"boolean"}')) 'Render-map MCP schema does not advertise executionWithinSelectedGeometry'
Assert-True ($renderMapBridgeSource.Contains('{ "plannedEventKinds", CSX::RenderMap::SerializeEventKindMask(kPlannedEventKinds) }')) 'Render-map registry does not distinguish planned event kinds'
Assert-True ($renderMapBridgeSource.Contains('{ "plannedKindsSelectable", false }')) 'Render-map registry does not declare planned event kinds non-selectable'
Assert-True ($renderMapBridgeSource.Contains('{ "defaults", {')) 'Render-map registry does not advertise the admitted default capture profile'
Assert-True ($renderMapBridgeSource.Contains('const auto maxBytes = a_args.value("maxBytes", kDefaultBytes);')) 'Render-map start does not default maxBytes to the admitted service budget'
Assert-True ($renderMapBridgeSource.Contains('constexpr std::uint64_t kMaximumBytes = 64ull * 1024ull * 1024ull;')) 'Render-map service storage ceiling is not the qualified 64 MiB envelope'
Assert-True ($renderMapBridgeSource.Contains('"maxBytes":{"type":"integer","minimum":1,"maximum":67108864}')) 'Render-map MCP schema does not advertise the 64 MiB storage ceiling'
Assert-True ($renderMapBridgeSource.Contains('{ "fixedCatalogueBytes", CSX::RenderMap::Collector::RequiredStorageBytes(defaultCatalogueConfig) }')) 'Render-map registry does not expose fixed default catalogue admission cost'
Assert-True ($renderMapBridgeSource.Contains('{ "minimumMaxBytes", fixedCatalogueBytes + 1 }')) 'Render-map invalid-bounds error does not report the minimum admitted byte budget'
Assert-True ($renderMapBridgeSource.Contains('constexpr std::uint32_t kContractMinor = 17;')) 'Render-map service contract does not advertise CPU-access minor 17'
Assert-True ($renderMapBridgeSource.Contains('constexpr std::uint32_t kSchemaRevision = 18;')) 'Render-map service contract does not advertise CPU-access schema revision 18'
Assert-True ($renderMapBridgeSource.Contains('"resource-cpu-access-v1"')) 'Render-map registry does not advertise resource-cpu-access-v1'
Assert-True ($d3dContextHooksSource.Contains('stl::detour_vfunc<14, ID3D11DeviceContext_Map>(a_context);')) 'Immediate-context Map is not installed at D3D11 vtable slot 14'
Assert-True ($d3dContextHooksSource.Contains('stl::detour_vfunc<15, ID3D11DeviceContext_Unmap>(a_context);')) 'Immediate-context Unmap is not installed at D3D11 vtable slot 15'

Assert-JsonSchema (Join-Path $exampleRoot 'engine-map.example.json') (Join-Path $schemaRoot 'engine-map.schema.json')
$engineMapSeedPath = Join-Path $renderMapRoot 'engine-map.skyrim-vr-1.4.15.main-menu-seed.json'
Assert-JsonSchema $engineMapSeedPath (Join-Path $schemaRoot 'engine-map.schema.json')
$priorArtCatalogPath = Join-Path $renderMapRoot 'prior-art-catalog.json'
Assert-JsonSchema $priorArtCatalogPath (Join-Path $schemaRoot 'prior-art-catalog.schema.json')
Assert-JsonSchema (Join-Path $exampleRoot 'capture-manifest.example.json') (Join-Path $schemaRoot 'capture-manifest.schema.json')
Assert-JsonSchema (Join-Path $exampleRoot 'render-graph.example.json') (Join-Path $schemaRoot 'render-graph.schema.json')

$shaderManifest = Get-Content -Raw -LiteralPath (Join-Path $repoRoot 'docs/development/shader-analysis/shader-manifest.generated.json') | ConvertFrom-Json
$engineMap = Get-Content -Raw -LiteralPath (Join-Path $exampleRoot 'engine-map.example.json') | ConvertFrom-Json
$captureManifest = Get-Content -Raw -LiteralPath (Join-Path $exampleRoot 'capture-manifest.example.json') | ConvertFrom-Json
$renderGraph = Get-Content -Raw -LiteralPath (Join-Path $exampleRoot 'render-graph.example.json') | ConvertFrom-Json

$shaderRefs = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
foreach ($record in @($shaderManifest.compileUnits) + @($shaderManifest.passes)) {
    $null = $shaderRefs.Add([string] $record.id)
}

$engineRefs = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
foreach ($entity in $engineMap.entities) {
    $null = $engineRefs.Add([string] $entity.id)
}

$engineEvidenceRefs = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
foreach ($evidence in $engineMap.evidence) {
    $null = $engineEvidenceRefs.Add([string] $evidence.id)
}

Assert-Unique @($engineMap.entities.id) 'Engine entity IDs'
Assert-Unique @($engineMap.relations.id) 'Engine relation IDs'
Assert-Unique @($engineMap.evidence.id) 'Engine evidence IDs'

foreach ($entity in $engineMap.entities) {
    foreach ($evidenceRef in $entity.evidenceRefs) {
        Assert-True ($engineEvidenceRefs.Contains([string] $evidenceRef)) "Engine entity $($entity.id) refers to missing evidence $evidenceRef"
    }
}

foreach ($relation in $engineMap.relations) {
    foreach ($endpoint in @($relation.from, $relation.to)) {
        $namespace, $value = ([string] $endpoint).Split(':', 2)
        if ($namespace -eq 'engine') {
            Assert-True ($engineRefs.Contains($value)) "Engine relation $($relation.id) refers to missing engine entity $value"
        }
        elseif ($namespace -eq 'shader') {
            Assert-True ($shaderRefs.Contains($value)) "Engine relation $($relation.id) refers to missing shader manifest ID $value"
        }
        else {
            throw "Engine relation $($relation.id) has unknown namespace $namespace"
        }
    }

    foreach ($evidenceRef in $relation.evidenceRefs) {
        Assert-True ($engineEvidenceRefs.Contains([string] $evidenceRef)) "Engine relation $($relation.id) refers to missing evidence $evidenceRef"
    }
}

$engineMapSeed = Get-Content -Raw -LiteralPath $engineMapSeedPath | ConvertFrom-Json
$priorArtCatalog = Get-Content -Raw -LiteralPath $priorArtCatalogPath | ConvertFrom-Json
$seedEngineRefs = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
foreach ($entity in $engineMapSeed.entities) {
    $null = $seedEngineRefs.Add([string] $entity.id)
}

$seedEvidenceRefs = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
foreach ($evidence in $engineMapSeed.evidence) {
    $null = $seedEvidenceRefs.Add([string] $evidence.id)
}

Assert-Unique @($engineMapSeed.entities.id) 'Seed engine entity IDs'
Assert-Unique @($engineMapSeed.relations.id) 'Seed engine relation IDs'
Assert-Unique @($engineMapSeed.evidence.id) 'Seed engine evidence IDs'

foreach ($entity in $engineMapSeed.entities) {
    foreach ($evidenceRef in $entity.evidenceRefs) {
        Assert-True ($seedEvidenceRefs.Contains([string] $evidenceRef)) "Seed engine entity $($entity.id) refers to missing evidence $evidenceRef"
    }
}

foreach ($relation in $engineMapSeed.relations) {
    foreach ($endpoint in @($relation.from, $relation.to)) {
        $namespace, $value = ([string] $endpoint).Split(':', 2)
        if ($namespace -eq 'engine') {
            Assert-True ($seedEngineRefs.Contains($value)) "Seed engine relation $($relation.id) refers to missing engine entity $value"
        }
        elseif ($namespace -eq 'shader') {
            Assert-True ($shaderRefs.Contains($value)) "Seed engine relation $($relation.id) refers to missing shader manifest ID $value"
        }
        else {
            throw "Seed engine relation $($relation.id) has unknown namespace $namespace"
        }
    }

    foreach ($evidenceRef in $relation.evidenceRefs) {
        Assert-True ($seedEvidenceRefs.Contains([string] $evidenceRef)) "Seed engine relation $($relation.id) refers to missing evidence $evidenceRef"
    }
}

$priorSourceRefs = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
foreach ($source in $priorArtCatalog.sources) {
    $null = $priorSourceRefs.Add([string] $source.id)
}

Assert-Unique @($priorArtCatalog.sources.id) 'Prior-art source IDs'
Assert-Unique @($priorArtCatalog.claims.id) 'Prior-art claim IDs'
$spotCheckIds = [System.Collections.Generic.List[string]]::new()
foreach ($claim in $priorArtCatalog.claims) {
    foreach ($spotCheck in $claim.targetAssessment.spotChecks) {
        $spotCheckIds.Add([string] $spotCheck.id)
    }
}
if ($spotCheckIds.Count -gt 0) {
    Assert-Unique @($spotCheckIds) 'Prior-art spot-check IDs'
}

$targetManifestPath = Join-Path $repoRoot ([string] $priorArtCatalog.target.shaderSystem.manifestPath)
Assert-True (Test-Path -LiteralPath $targetManifestPath -PathType Leaf) "Prior-art target manifest does not exist: $targetManifestPath"
$targetManifestHash = (Get-FileHash -LiteralPath $targetManifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
Assert-True ($targetManifestHash -eq ([string] $priorArtCatalog.target.shaderSystem.manifestSha256).ToLowerInvariant()) 'Prior-art target manifest hash does not match the declared file'
Assert-True ([string] $priorArtCatalog.target.engine.executableSha256 -eq [string] $engineMapSeed.engineBuild.executable.sha256) 'Prior-art target executable hash does not match the seed engine map'
Assert-True ([string] $priorArtCatalog.target.engine.runtime -eq [string] $engineMapSeed.engineBuild.runtime) 'Prior-art target runtime does not match the seed engine map'

foreach ($source in $priorArtCatalog.sources) {
    foreach ($lineageRef in $source.lineageRefs) {
        Assert-True ($priorSourceRefs.Contains([string] $lineageRef)) "Prior-art source $($source.id) refers to missing lineage source $lineageRef"
        Assert-True ([string] $lineageRef -ne [string] $source.id) "Prior-art source $($source.id) refers to itself as lineage"
    }
}

foreach ($claim in $priorArtCatalog.claims) {
    Assert-True ($priorSourceRefs.Contains([string] $claim.sourceRef)) "Prior-art claim $($claim.id) refers to missing source $($claim.sourceRef)"

    foreach ($targetRef in $claim.targetRefs) {
        $namespace, $value = ([string] $targetRef).Split(':', 2)
        if ($namespace -eq 'engine') {
            Assert-True ($seedEngineRefs.Contains($value)) "Prior-art claim $($claim.id) refers to missing seed engine entity $value"
        }
        elseif ($namespace -eq 'shader') {
            Assert-True ($shaderRefs.Contains($value)) "Prior-art claim $($claim.id) refers to missing shader manifest ID $value"
        }
        else {
            throw "Prior-art claim $($claim.id) has unknown target namespace $namespace"
        }
    }

    foreach ($spotCheck in $claim.targetAssessment.spotChecks) {
        foreach ($evidenceRef in $spotCheck.evidenceRefs) {
            Assert-True ($seedEvidenceRefs.Contains([string] $evidenceRef)) "Prior-art spot check $($spotCheck.id) refers to missing seed evidence $evidenceRef"
        }
    }
}

$events = @()
$eventSchema = Join-Path $schemaRoot 'render-event.schema.json'
$mutationEventPath = Join-Path $exampleRoot 'resource-mutation-events.example.jsonl'
$mutationEventCount = 0
foreach ($line in Get-Content -LiteralPath $mutationEventPath) {
    $mutationEventCount++
    Assert-True ($line | Test-Json -SchemaFile $eventSchema) "Resource mutation event line $mutationEventCount does not validate"
}
Assert-True ($mutationEventCount -gt 0) 'The resource mutation example stream is empty'

$eventPath = Join-Path $exampleRoot 'opaque-lighting-events.example.jsonl'
$lineNumber = 0
foreach ($line in Get-Content -LiteralPath $eventPath) {
    $lineNumber++
    Assert-True ($line | Test-Json -SchemaFile $eventSchema) "Event line $lineNumber does not validate"
    $events += ($line | ConvertFrom-Json)
}

Assert-True ($events.Count -gt 0) 'The example event stream is empty'
Assert-Unique @($events.sequence) 'Event sequences'

$eventSequences = [System.Collections.Generic.HashSet[long]]::new()
$knownObservations = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
$previousSequence = -1L

foreach ($event in $events) {
    $sequence = [long] $event.sequence
    Assert-True ($sequence -gt $previousSequence) "Event sequence $sequence is not strictly increasing"
    Assert-True ([string] $event.captureId -eq [string] $captureManifest.captureId) "Event $sequence belongs to a different capture"

    foreach ($cause in $event.causes) {
        Assert-True ([long] $cause -lt $sequence) "Event $sequence refers to a non-prior cause $cause"
        Assert-True ($eventSequences.Contains([long] $cause)) "Event $sequence refers to missing cause $cause"
    }

    foreach ($manifestRef in $event.manifestRefs) {
        Assert-True ($shaderRefs.Contains([string] $manifestRef)) "Event $sequence refers to missing shader manifest ID $manifestRef"
    }

    foreach ($engineRef in $event.engineRefs) {
        Assert-True ($engineRefs.Contains([string] $engineRef)) "Event $sequence refers to missing engine entity $engineRef"
    }

    foreach ($observation in $event.observationRefs) {
        $null = $knownObservations.Add([string] $observation.id)
    }

    foreach ($scopeName in @('renderPass', 'technique', 'geometry', 'commandList')) {
        $scopeValue = $event.scopes.$scopeName
        if ($null -ne $scopeValue) {
            Assert-True ($knownObservations.Contains([string] $scopeValue)) "Event $sequence has an unknown $scopeName scope $scopeValue"
        }
    }

    if ($null -ne $event.deviceContextObservationId) {
        Assert-True ($knownObservations.Contains([string] $event.deviceContextObservationId)) "Event $sequence has an unknown device context"
    }

    $null = $eventSequences.Add($sequence)
    $previousSequence = $sequence
}

Assert-True ([int] $captureManifest.completion.eventCount -eq $events.Count) 'Capture event count does not match the event stream'
Assert-True ([long] $captureManifest.completion.firstSequence -eq [long] $events[0].sequence) 'Capture first sequence does not match the event stream'
Assert-True ([long] $captureManifest.completion.lastSequence -eq [long] $events[-1].sequence) 'Capture last sequence does not match the event stream'

$graphNodeIds = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
foreach ($node in $renderGraph.nodes) {
    $null = $graphNodeIds.Add([string] $node.id)
}

Assert-Unique @($renderGraph.nodes.id) 'Render graph node IDs'
Assert-Unique @($renderGraph.edges.id) 'Render graph edge IDs'

$graphEdgeIds = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
foreach ($edge in $renderGraph.edges) {
    $null = $graphEdgeIds.Add([string] $edge.id)
    Assert-True ($graphNodeIds.Contains([string] $edge.from)) "Graph edge $($edge.id) has a missing source node"
    Assert-True ($graphNodeIds.Contains([string] $edge.to)) "Graph edge $($edge.id) has a missing target node"

    foreach ($evidence in $edge.evidence) {
        foreach ($sequence in $evidence.eventSequences) {
            Assert-True ($eventSequences.Contains([long] $sequence)) "Graph edge $($edge.id) refers to missing event $sequence"
        }
    }
}

foreach ($ambiguity in $renderGraph.ambiguities) {
    foreach ($edgeId in $ambiguity.candidateEdgeIds) {
        Assert-True ($graphEdgeIds.Contains([string] $edgeId)) "Ambiguity $($ambiguity.id) refers to missing edge $edgeId"
    }
}

foreach ($window in $renderGraph.decisionWindows) {
    Assert-True ($graphNodeIds.Contains([string] $window.candidateNode)) "Decision window $($window.id) refers to a missing candidate node"
    Assert-True ($eventSequences.Contains([long] $window.visibilityAvailable.sequence)) "Decision window $($window.id) has a missing availability event"
    Assert-True ($eventSequences.Contains([long] $window.decisionDeadline.sequence)) "Decision window $($window.id) has a missing deadline event"
    if ($null -ne $window.eyeCoverage) {
        foreach ($route in $window.eyeCoverage.routes) {
            Assert-True ($graphNodeIds.Contains([string] $route.eyeSubmitNode)) "Decision window $($window.id) has an eye route with a missing submit node"
            foreach ($sequence in $route.eventSequences) {
                Assert-True ($eventSequences.Contains([long] $sequence)) "Decision window $($window.id) has an eye route referring to missing event $sequence"
            }
        }
        foreach ($ambiguityId in $window.eyeCoverage.ambiguityIds) {
            Assert-True (@($renderGraph.ambiguities.id) -contains [string] $ambiguityId) "Decision window $($window.id) refers to missing ambiguity $ambiguityId"
        }
    }
}

Write-Output "Render-map contracts passed: $($schemaFiles.Count) schemas, 1 prior-art catalogue with $($priorArtCatalog.claims.Count) claims, 2 engine maps, $($events.Count + $mutationEventCount) events, $($renderGraph.nodes.Count) graph nodes."
