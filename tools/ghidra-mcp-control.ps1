# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet("start", "status", "stop")]
    [string] $Action = "status",
    [string] $GhidraInstallDir,
    [string] $GhidraUserExtensionsDir,
    [string] $JavaHome,
    [string] $ProjectDirectory,
    [ValidatePattern("^[A-Za-z0-9_.-]+$")]
    [string] $ProjectName = "SkyrimVRMcp",
    [string] $ProgramPath,
    [ValidatePattern('^[^\\/:*?"<>|]+$')]
    [string] $ProgramName = "SkyrimVR.exe",
    [ValidateRange(1, 65535)]
    [int] $Port = 8080,
    [ValidateRange(1, 3600)]
    [int] $ReadyTimeoutSeconds = 30,
    [ValidateRange(1, 300)]
    [int] $StopTimeoutSeconds = 30,
    [string] $StateDirectory,
    [switch] $NoExit,
    [switch] $Compact
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$loopbackHost = "127.0.0.1"
$endpoint = "http://${loopbackHost}:$Port/mcp"

function Resolve-ExistingDirectory {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][string] $Description
    )

    $item = Get-Item -LiteralPath $Path -ErrorAction Stop
    if (-not $item.PSIsContainer) {
        throw "$Description is not a directory: $Path"
    }
    return $item.FullName
}

function Get-UnresolvedFullPath([string] $Path) {
    return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
}

function Assert-ChildPath {
    param(
        [Parameter(Mandatory = $true)][string] $Parent,
        [Parameter(Mandatory = $true)][string] $Candidate
    )

    $parentPrefix = [IO.Path]::GetFullPath($Parent).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    ) + [IO.Path]::DirectorySeparatorChar
    $candidatePath = [IO.Path]::GetFullPath($Candidate)
    if (-not $candidatePath.StartsWith(
        $parentPrefix,
        [StringComparison]::OrdinalIgnoreCase
    )) {
        throw "Controller path escapes its state directory: $candidatePath"
    }
}

function Write-AtomicJson {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)] $Value
    )

    $parent = Split-Path -Parent $Path
    $temporaryPath = Join-Path $parent (
        [IO.Path]::GetFileName($Path) + ".partial-$PID"
    )
    Assert-ChildPath -Parent $parent -Candidate $temporaryPath
    $Value |
        ConvertTo-Json -Depth 20 |
        Set-Content -LiteralPath $temporaryPath -Encoding utf8
    Move-Item -LiteralPath $temporaryPath -Destination $Path -Force
}

function Get-DefaultStateDirectory {
    if ($env:LOCALAPPDATA) {
        return Join-Path $env:LOCALAPPDATA "CommunityShaders\GhidraMCP\port-$Port"
    }
    return Join-Path $repositoryRoot ".git\csx-tools\ghidra-mcp\port-$Port"
}

function Resolve-JavaHome([string] $RequestedJavaHome) {
    $candidate = $RequestedJavaHome
    if (-not $candidate) {
        $candidate = $env:GHIDRA_JAVA_HOME
    }
    if (-not $candidate) {
        $candidate = $env:JAVA_HOME
    }
    if (-not $candidate) {
        $javaCommand = Get-Command java -ErrorAction Stop
        $candidate = Split-Path -Parent (Split-Path -Parent $javaCommand.Source)
    }

    $resolved = Resolve-ExistingDirectory -Path $candidate -Description "Java home"
    $javaName = if ($env:OS -eq "Windows_NT") { "java.exe" } else { "java" }
    $javaExecutable = Join-Path $resolved "bin\$javaName"
    if (-not (Test-Path -LiteralPath $javaExecutable -PathType Leaf)) {
        throw "Java executable was not found under $resolved."
    }
    return $resolved
}

function Resolve-GhidraExtensionDirectory {
    param(
        [Parameter(Mandatory = $true)][string] $GhidraRoot,
        [string] $RequestedDirectory
    )

    if ($RequestedDirectory) {
        $candidate = Resolve-ExistingDirectory `
            -Path $RequestedDirectory `
            -Description "GhidrAssistMCP extension"
    } elseif ($env:OS -eq "Windows_NT" -and $env:APPDATA) {
        $profileName = Split-Path -Leaf $GhidraRoot
        $candidate = Join-Path `
            $env:APPDATA `
            "ghidra\$profileName\Extensions\GhidrAssistMCP"
        $candidate = Resolve-ExistingDirectory `
            -Path $candidate `
            -Description "GhidrAssistMCP extension"
    } else {
        throw "Pass -GhidraUserExtensionsDir on this platform."
    }

    $serverScript = Join-Path $candidate "ghidra_scripts\GAMCPStartServerScript.java"
    if (-not (Test-Path -LiteralPath $serverScript -PathType Leaf)) {
        throw "GAMCPStartServerScript.java was not found under $candidate."
    }
    return $candidate
}

function Get-McpResponseJson([string] $Content) {
    $trimmed = $Content.Trim()
    if ($trimmed.StartsWith("{")) {
        return $trimmed | ConvertFrom-Json -Depth 30
    }

    foreach ($line in $trimmed -split "`r?`n") {
        if ($line.StartsWith("data:")) {
            $payload = $line.Substring(5).Trim()
            if ($payload.StartsWith("{")) {
                return $payload | ConvertFrom-Json -Depth 30
            }
        }
    }
    throw "The MCP endpoint returned an unrecognized response."
}

function Test-LoopbackPort {
    $client = [Net.Sockets.TcpClient]::new()
    try {
        $connect = $client.ConnectAsync($loopbackHost, $Port)
        if (-not $connect.Wait(250)) {
            return $false
        }
        return $client.Connected
    } catch {
        return $false
    } finally {
        $client.Dispose()
    }
}

function Test-GhidrAssistEndpoint {
    if (-not (Test-LoopbackPort)) {
        return [pscustomobject][ordered]@{
            ready = $false
            serverName = $null
            protocolVersion = $null
            error = "The loopback endpoint is not listening."
        }
    }

    $headers = @{
        Accept = "application/json, text/event-stream"
        "Content-Type" = "application/json"
    }
    $body = @{
        jsonrpc = "2.0"
        id = [DateTime]::UtcNow.Ticks
        method = "initialize"
        params = @{
            protocolVersion = "2025-03-26"
            capabilities = @{}
            clientInfo = @{
                name = "CSXGhidraMCPControl"
                version = "1.0"
            }
        }
    } | ConvertTo-Json -Depth 10 -Compress

    try {
        $response = Invoke-WebRequest `
            -UseBasicParsing `
            -Method Post `
            -Uri $endpoint `
            -Headers $headers `
            -Body $body `
            -TimeoutSec 3
        $json = Get-McpResponseJson -Content $response.Content
        $serverName = [string] $json.result.serverInfo.name
        $sessionHeader = $response.Headers["Mcp-Session-Id"]
        $sessionId = if ($sessionHeader -is [array]) {
            [string] $sessionHeader[0]
        } else {
            [string] $sessionHeader
        }

        if ($sessionId) {
            try {
                Invoke-WebRequest `
                    -UseBasicParsing `
                    -Method Delete `
                    -Uri $endpoint `
                    -Headers @{ "Mcp-Session-Id" = $sessionId } `
                    -TimeoutSec 3 | Out-Null
            } catch {
                # Session cleanup is best-effort after identity has been proven.
            }
        }

        return [pscustomobject][ordered]@{
            ready = $serverName -eq "ghidrassistmcp"
            serverName = $serverName
            protocolVersion = [string] $json.result.protocolVersion
            error = $null
        }
    } catch {
        return [pscustomobject][ordered]@{
            ready = $false
            serverName = $null
            protocolVersion = $null
            error = $_.Exception.Message
        }
    }
}

function Get-ListenerPid {
    $owners = @()
    if (Get-Command Get-NetTCPConnection -ErrorAction SilentlyContinue) {
        try {
            $owners = @(
                Get-NetTCPConnection `
                    -State Listen `
                    -LocalPort $Port `
                    -ErrorAction Stop |
                    Where-Object LocalAddress -In @("127.0.0.1", "::1") |
                    Select-Object -ExpandProperty OwningProcess -Unique
            )
            if ($owners.Count -eq 1) {
                return [int] $owners[0]
            }
        } catch {
            $owners = @()
        }
    }

    if ($env:OS -eq "Windows_NT") {
        $pattern = (
            "^\s*TCP\s+(127\.0\.0\.1|\[::1\]):$Port\s+" +
            ".*LISTENING\s+(?<pid>\d+)\s*$"
        )
        $owners = @(
            netstat -ano |
                ForEach-Object {
                    if ($_ -match $pattern) {
                        [int] $Matches.pid
                    }
                } |
                Sort-Object -Unique
        )
        if ($owners.Count -eq 1) {
            return [int] $owners[0]
        }
    }
    return $null
}

function Get-TrackedProcess($Session) {
    if ($null -eq $Session -or $null -eq $Session.processId) {
        return $null
    }

    try {
        $process = Get-Process -Id ([int] $Session.processId) -ErrorAction Stop
        $actualStart = $process.StartTime.ToUniversalTime()
        $expectedValue = $Session.processStartUtc
        $expectedStart = if ($expectedValue -is [DateTime]) {
            $expectedValue.ToUniversalTime()
        } elseif ($expectedValue -is [DateTimeOffset]) {
            $expectedValue.UtcDateTime
        } else {
            [DateTimeOffset]::Parse(
                [string] $expectedValue,
                [Globalization.CultureInfo]::InvariantCulture,
                [Globalization.DateTimeStyles]::RoundtripKind
            ).UtcDateTime
        }
        if ([Math]::Abs(($actualStart - $expectedStart).TotalSeconds) -gt 1) {
            return $null
        }
        return $process
    } catch {
        return $null
    }
}

function Test-DescendantProcess {
    param(
        [Parameter(Mandatory = $true)][int] $ProcessId,
        [Parameter(Mandatory = $true)][int] $AncestorProcessId
    )

    if ($ProcessId -eq $AncestorProcessId) {
        return $true
    }
    if ($env:OS -ne "Windows_NT") {
        return $null
    }

    $current = $ProcessId
    for ($depth = 0; $depth -lt 12; $depth++) {
        try {
            $record = Get-CimInstance `
                -ClassName Win32_Process `
                -Filter "ProcessId = $current" `
                -ErrorAction Stop
        } catch {
            return $null
        }
        if ($null -eq $record -or [int] $record.ParentProcessId -le 0) {
            return $false
        }
        $current = [int] $record.ParentProcessId
        if ($current -eq $AncestorProcessId) {
            return $true
        }
    }
    return $false
}

function Read-Session([string] $SessionFile) {
    if (-not (Test-Path -LiteralPath $SessionFile -PathType Leaf)) {
        return $null
    }
    return Get-Content -LiteralPath $SessionFile -Raw | ConvertFrom-Json
}

function Get-OptionalProperty {
    param(
        $Value,
        [Parameter(Mandatory = $true)][string] $Name
    )

    if ($null -eq $Value) {
        return $null
    }
    $property = $Value.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return $null
    }
    return $property.Value
}

function New-StatusResult {
    param(
        [Parameter(Mandatory = $true)][string] $State,
        [Parameter(Mandatory = $true)][bool] $Ok,
        $Session,
        $Process,
        $Probe,
        [string[]] $Warnings = @(),
        [string[]] $Errors = @()
    )

    $listenerPid = Get-ListenerPid
    $ownedListener = $null
    if ($listenerPid -and $Process) {
        $ownedListener = Test-DescendantProcess `
            -ProcessId $listenerPid `
            -AncestorProcessId $Process.Id
    }
    $resultErrors = [Collections.Generic.List[string]]::new()
    foreach ($errorMessage in $Errors) {
        $resultErrors.Add($errorMessage)
    }
    $effectiveOk = $Ok
    if ($Probe.ready -and $Process -and $ownedListener -ne $true) {
        $effectiveOk = $false
        $resultErrors.Add(
            "The MCP listener could not be proven to belong to the managed Ghidra process tree."
        )
    }

    return [pscustomobject][ordered]@{
        schemaVersion = 1
        ok = $effectiveOk
        action = $Action
        state = $State
        managed = $null -ne $Process
        endpoint = $endpoint
        endpointReady = [bool] $Probe.ready
        serverName = $Probe.serverName
        processId = $(if ($Process) { $Process.Id } else { $null })
        listenerProcessId = $listenerPid
        listenerOwnedBySession = $ownedListener
        project = $(if ($Session) {
            [pscustomobject][ordered]@{
                directory = $Session.projectDirectory
                name = $Session.projectName
                program = $Session.programName
                mode = $Session.mode
            }
        } else { $null })
        logs = $(if ($Session) {
            [pscustomobject][ordered]@{
                stdout = $Session.stdoutLog
                stderr = $Session.stderrLog
                exitReceipt = $Session.exitReceipt
            }
        } else { $null })
        warnings = @(
            $Warnings |
                Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
        )
        errors = @($resultErrors)
    }
}

function Wait-ForReady {
    param(
        [Parameter(Mandatory = $true)] $Session,
        [Parameter(Mandatory = $true)][int] $TimeoutSeconds
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $lastProbe = Test-GhidrAssistEndpoint
    while (-not $lastProbe.ready -and [DateTime]::UtcNow -lt $deadline) {
        $process = Get-TrackedProcess -Session $Session
        if (-not $process) {
            break
        }
        Start-Sleep -Milliseconds 500
        $lastProbe = Test-GhidrAssistEndpoint
    }
    return $lastProbe
}

function Complete-Command($Result) {
    $Result | ConvertTo-Json -Depth 20 -Compress:$Compact
    if (-not $Result.ok -and -not $NoExit) {
        exit 2
    }
}

$resolvedStateDirectory = if ($StateDirectory) {
    Get-UnresolvedFullPath $StateDirectory
} else {
    Get-DefaultStateDirectory
}
$sessionFile = Join-Path $resolvedStateDirectory "session.json"
$launchFile = Join-Path $resolvedStateDirectory "launch.json"
$completionFile = Join-Path $resolvedStateDirectory "session.complete"
$stdoutLog = Join-Path $resolvedStateDirectory "headless.stdout.log"
$stderrLog = Join-Path $resolvedStateDirectory "headless.stderr.log"
$exitReceipt = Join-Path $resolvedStateDirectory "exit.json"

try {
    $session = Read-Session -SessionFile $sessionFile
    if ($Action -eq "start" -and $session) {
        if (-not $PSBoundParameters.ContainsKey("GhidraInstallDir")) {
            $GhidraInstallDir = Get-OptionalProperty `
                -Value $session `
                -Name "ghidraInstallDir"
        }
        if (-not $PSBoundParameters.ContainsKey("GhidraUserExtensionsDir")) {
            $GhidraUserExtensionsDir = Get-OptionalProperty `
                -Value $session `
                -Name "ghidraUserExtensionsDir"
        }
        if (-not $PSBoundParameters.ContainsKey("JavaHome")) {
            $JavaHome = Get-OptionalProperty -Value $session -Name "javaHome"
        }
        if (-not $PSBoundParameters.ContainsKey("ProjectDirectory")) {
            $ProjectDirectory = Get-OptionalProperty `
                -Value $session `
                -Name "projectDirectory"
        }
        if (-not $PSBoundParameters.ContainsKey("ProjectName")) {
            $savedProjectName = Get-OptionalProperty `
                -Value $session `
                -Name "projectName"
            if ($savedProjectName) {
                $ProjectName = $savedProjectName
            }
        }
        if (-not $PSBoundParameters.ContainsKey("ProgramPath")) {
            $ProgramPath = Get-OptionalProperty `
                -Value $session `
                -Name "programPath"
        }
        if (-not $PSBoundParameters.ContainsKey("ProgramName")) {
            $savedProgramName = Get-OptionalProperty `
                -Value $session `
                -Name "programName"
            if ($savedProgramName) {
                $ProgramName = $savedProgramName
            }
        }
    }
    $trackedProcess = Get-TrackedProcess -Session $session
    $probe = Test-GhidrAssistEndpoint

    if ($Action -eq "status") {
        if ($trackedProcess -and $probe.ready) {
            Complete-Command (New-StatusResult `
                -State "ready" `
                -Ok $true `
                -Session $session `
                -Process $trackedProcess `
                -Probe $probe)
        } elseif ($trackedProcess) {
            Complete-Command (New-StatusResult `
                -State "starting" `
                -Ok $true `
                -Session $session `
                -Process $trackedProcess `
                -Probe $probe)
        } elseif ($probe.ready) {
            Complete-Command (New-StatusResult `
                -State "external_ready" `
                -Ok $true `
                -Session $null `
                -Process $null `
                -Probe $probe `
                -Warnings @("A GhidrAssistMCP server is ready but is not owned by this controller."))
        } else {
            Complete-Command (New-StatusResult `
                -State "stopped" `
                -Ok $true `
                -Session $session `
                -Process $null `
                -Probe $probe)
        }
        return
    }

    if ($Action -eq "stop") {
        if (-not $trackedProcess) {
            $warnings = if ($probe.ready) {
                @("The live endpoint is unmanaged and was left running.")
            } else {
                @()
            }
            Complete-Command (New-StatusResult `
                -State $(if ($probe.ready) { "external_ready" } else { "already_stopped" }) `
                -Ok $true `
                -Session $session `
                -Process $null `
                -Probe $probe `
                -Warnings $warnings)
            return
        }

        Set-Content -LiteralPath $completionFile -Value "" -Encoding ascii
        $deadline = [DateTime]::UtcNow.AddSeconds($StopTimeoutSeconds)
        do {
            Start-Sleep -Milliseconds 500
            $trackedProcess = Get-TrackedProcess -Session $session
        } while ($trackedProcess -and [DateTime]::UtcNow -lt $deadline)

        $probe = Test-GhidrAssistEndpoint
        if ($trackedProcess) {
            Complete-Command (New-StatusResult `
                -State "stop_timeout" `
                -Ok $false `
                -Session $session `
                -Process $trackedProcess `
                -Probe $probe `
                -Errors @("Ghidra did not stop within $StopTimeoutSeconds seconds; it was not terminated."))
        } else {
            Complete-Command (New-StatusResult `
                -State "stopped" `
                -Ok (-not $probe.ready) `
                -Session $session `
                -Process $null `
                -Probe $probe `
                -Errors $(if ($probe.ready) {
                    @("The managed process stopped, but another GhidrAssistMCP server still owns the endpoint.")
                } else { @() }))
        }
        return
    }

    if ($trackedProcess) {
        if (-not $probe.ready) {
            $probe = Wait-ForReady `
                -Session $session `
                -TimeoutSeconds $ReadyTimeoutSeconds
            $trackedProcess = Get-TrackedProcess -Session $session
        }
        Complete-Command (New-StatusResult `
            -State $(if ($probe.ready) { "ready" } else { "starting" }) `
            -Ok ($null -ne $trackedProcess) `
            -Session $session `
            -Process $trackedProcess `
            -Probe $probe `
            -Warnings $(if (-not $probe.ready -and $trackedProcess) {
                @("Ghidra is still importing or analyzing; inspect the logs and call status again.")
            } else { @() }))
        return
    }

    if ($probe.ready) {
        Complete-Command (New-StatusResult `
            -State "external_ready" `
            -Ok $false `
            -Session $null `
            -Process $null `
            -Probe $probe `
            -Errors @("Port $Port already hosts an unmanaged GhidrAssistMCP server; refusing to claim it."))
        return
    }
    $unmanagedListenerPid = Get-ListenerPid
    if ($unmanagedListenerPid) {
        throw (
            "Port $Port is already owned by PID $unmanagedListenerPid, but it " +
            "did not identify as GhidrAssistMCP."
        )
    }

    if (-not $GhidraInstallDir) {
        throw "Start requires -GhidraInstallDir."
    }
    $resolvedGhidraRoot = Resolve-ExistingDirectory `
        -Path $GhidraInstallDir `
        -Description "Ghidra installation"
    $analyzeHeadlessName = if ($env:OS -eq "Windows_NT") {
        "analyzeHeadless.bat"
    } else {
        "analyzeHeadless"
    }
    $analyzeHeadless = Join-Path `
        $resolvedGhidraRoot `
        "support\$analyzeHeadlessName"
    if (-not (Test-Path -LiteralPath $analyzeHeadless -PathType Leaf)) {
        throw "Ghidra analyzeHeadless was not found: $analyzeHeadless"
    }

    $resolvedJavaHome = Resolve-JavaHome -RequestedJavaHome $JavaHome
    $extensionDirectory = Resolve-GhidraExtensionDirectory `
        -GhidraRoot $resolvedGhidraRoot `
        -RequestedDirectory $GhidraUserExtensionsDir
    $controllerScriptDirectory = Join-Path $repositoryRoot "tools\ghidra_scripts"
    $controllerServerScript = Join-Path `
        $controllerScriptDirectory `
        "CSXGAMCPStartServerScript.java"
    if (-not (Test-Path -LiteralPath $controllerServerScript -PathType Leaf)) {
        throw "The controller's Ghidra server adapter was not found: $controllerServerScript"
    }

    New-Item -ItemType Directory -Path $resolvedStateDirectory -Force | Out-Null
    $resolvedStateDirectory = Resolve-ExistingDirectory `
        -Path $resolvedStateDirectory `
        -Description "Ghidra MCP state directory"
    foreach ($ownedPath in @(
        $launchFile,
        $completionFile,
        $stdoutLog,
        $stderrLog,
        $exitReceipt
    )) {
        Assert-ChildPath -Parent $resolvedStateDirectory -Candidate $ownedPath
        if (Test-Path -LiteralPath $ownedPath) {
            Remove-Item -LiteralPath $ownedPath -Force
        }
    }

    $resolvedProjectDirectory = if ($ProjectDirectory) {
        Get-UnresolvedFullPath $ProjectDirectory
    } else {
        Join-Path $resolvedStateDirectory "projects"
    }
    New-Item -ItemType Directory -Path $resolvedProjectDirectory -Force | Out-Null
    $resolvedProjectDirectory = Resolve-ExistingDirectory `
        -Path $resolvedProjectDirectory `
        -Description "Ghidra project directory"
    $projectFile = Join-Path $resolvedProjectDirectory "$ProjectName.gpr"
    $projectExists = Test-Path -LiteralPath $projectFile -PathType Leaf
    $resolvedProgramPath = $null
    if ($ProgramPath) {
        $resolvedProgramPath = (Get-Item -LiteralPath $ProgramPath -ErrorAction Stop).FullName
        if (-not (Test-Path -LiteralPath $resolvedProgramPath -PathType Leaf)) {
            throw "Program is not a file: $resolvedProgramPath"
        }
        if (-not $PSBoundParameters.ContainsKey("ProgramName")) {
            $ProgramName = Split-Path -Leaf $resolvedProgramPath
        }
    }
    if (-not $projectExists -and -not $resolvedProgramPath) {
        throw "The project does not exist; first start requires -ProgramPath."
    }

    $mode = if ($projectExists) { "process" } else { "import" }
    $arguments = [Collections.Generic.List[string]]::new()
    $arguments.Add($resolvedProjectDirectory)
    $arguments.Add($ProjectName)
    if ($mode -eq "import") {
        $arguments.Add("-import")
        $arguments.Add($resolvedProgramPath)
    } else {
        $arguments.Add("-process")
        $arguments.Add($ProgramName)
        $arguments.Add("-noanalysis")
    }
    $arguments.Add("-scriptPath")
    $arguments.Add($controllerScriptDirectory)
    $arguments.Add("-postScript")
    $arguments.Add("CSXGAMCPStartServerScript.java")
    $arguments.Add("host=$loopbackHost")
    $arguments.Add("port=$Port")
    $arguments.Add("wait=true")
    $arguments.Add("completion_file=$completionFile")

    $launch = [ordered]@{
        schemaVersion = 1
        analyzeHeadless = $analyzeHeadless
        arguments = @($arguments)
        javaHome = $resolvedJavaHome
        workingDirectory = $repositoryRoot
        stdoutLog = $stdoutLog
        stderrLog = $stderrLog
        exitReceipt = $exitReceipt
    }
    Write-AtomicJson -Path $launchFile -Value $launch

    $hostScript = Join-Path $PSScriptRoot "ghidra-mcp-headless-host.ps1"
    $powerShell = Get-Command pwsh -ErrorAction Stop
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $powerShell.Source
    $startInfo.WorkingDirectory = $repositoryRoot
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    foreach ($argument in @(
        "-NoLogo",
        "-NoProfile",
        "-NonInteractive",
        "-File",
        $hostScript,
        "-LaunchFile",
        $launchFile
    )) {
        $null = $startInfo.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "Unable to start the Ghidra headless host."
    }

    $session = [ordered]@{
        schemaVersion = 1
        processId = $process.Id
        processStartUtc = $process.StartTime.ToUniversalTime().ToString("o")
        startedUtc = [DateTime]::UtcNow.ToString("o")
        endpoint = $endpoint
        ghidraInstallDir = $resolvedGhidraRoot
        ghidraUserExtensionsDir = $extensionDirectory
        javaHome = $resolvedJavaHome
        projectDirectory = $resolvedProjectDirectory
        projectName = $ProjectName
        programPath = $resolvedProgramPath
        programName = $ProgramName
        mode = $mode
        completionFile = $completionFile
        stdoutLog = $stdoutLog
        stderrLog = $stderrLog
        exitReceipt = $exitReceipt
    }
    Write-AtomicJson -Path $sessionFile -Value $session
    $session = Read-Session -SessionFile $sessionFile

    $probe = Wait-ForReady `
        -Session $session `
        -TimeoutSeconds $ReadyTimeoutSeconds
    $trackedProcess = Get-TrackedProcess -Session $session
    $state = if ($probe.ready) {
        "ready"
    } elseif ($trackedProcess) {
        "starting"
    } else {
        "failed"
    }
    $errors = if ($state -eq "failed") {
        @("Ghidra exited before the MCP endpoint became ready; inspect the logs.")
    } else {
        @()
    }
    $warnings = if ($state -eq "starting") {
        @("Ghidra is still importing or analyzing; inspect the logs and call status again.")
    } else {
        @()
    }
    Complete-Command (New-StatusResult `
        -State $state `
        -Ok ($state -ne "failed") `
        -Session $session `
        -Process $trackedProcess `
        -Probe $probe `
        -Warnings $warnings `
        -Errors $errors)
} catch {
    Complete-Command ([pscustomobject][ordered]@{
        schemaVersion = 1
        ok = $false
        action = $Action
        state = "error"
        endpoint = $endpoint
        warnings = @()
        errors = @($_.Exception.Message)
    })
}
