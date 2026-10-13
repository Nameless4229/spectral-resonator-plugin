$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot 'Spectral Foundry.vst3'
$destinationRoot = Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'
$destination = Join-Path $destinationRoot 'Spectral Foundry.vst3'
if (-not (Test-Path -LiteralPath (Join-Path $source 'Contents\x86_64-win\Spectral Foundry.vst3'))) {
    throw 'Plugin bundle missing. Extract the entire release ZIP before running Install.cmd.'
}
if (Test-Path -LiteralPath $destination) {
    throw "A copy already exists at $destination. Close your DAW and move that copy aside before installing."
}
New-Item -ItemType Directory -Path $destinationRoot -Force | Out-Null
Copy-Item -LiteralPath $source -Destination $destination -Recurse
Write-Host "Installed Spectral Foundry to $destination"
Write-Host 'Rescan VST3 plugins in your DAW. If needed, add the folder above to its VST3 search paths.'
