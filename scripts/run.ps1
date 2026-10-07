param([string]$Media)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskApp = Join-Path $taskRoot 'out\TrieFrameViewer\TrieFrameViewer.exe'
if (-not (Test-Path -LiteralPath $taskApp)) { throw 'Deploy first: python scripts\dev.py deploy' }
if ($Media) { & $taskApp $Media } else { & $taskApp }
