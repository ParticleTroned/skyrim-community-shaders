Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
if ($args.Count -eq 0) {
    throw "Supply an executable followed by its arguments."
}
$command = [string] $args[0]
$commandArguments = @($args | Select-Object -Skip 1)
. (Join-Path $PSScriptRoot "tool-environment.ps1")
Initialize-CsxMsvcEnvironment -Required | Out-Null
& $command @commandArguments
exit $LASTEXITCODE
