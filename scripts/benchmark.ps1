param(
    [string]$AppPath = 'out\TrieFrameViewer\TrieFrameViewer.exe',
    [string[]]$MediaPaths = @('artifacts\reference-test.mp4', 'artifacts\reference-720.mp4', 'artifacts\seek-stress.mp4', 'artifacts\long-4000.mp4'),
    [string]$Label = 'current',
    [int]$CacheMiB = 512,
    [string]$DecodeMode = '',
    [int]$DecoderThreads = 2
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$env:PATH = (Join-Path $taskRoot '.tools\Qt\6.8.3\msvc2022_64\bin') + ';' + (Join-Path $taskRoot '.tools\ffmpeg\bin') + ';' + $env:PATH
$taskReports = Join-Path $taskRoot ('artifacts\performance\' + $Label)
New-Item -ItemType Directory -Force -Path $taskReports | Out-Null
$taskApp = (Resolve-Path -LiteralPath $AppPath).Path
foreach ($taskMedia in $MediaPaths) {
    $taskSource = (Resolve-Path -LiteralPath $taskMedia).Path
    $taskName = [IO.Path]::GetFileNameWithoutExtension($taskSource)
    $taskReport = Join-Path $taskReports ($taskName + '.json')
    $taskArguments = @(('"' + $taskSource + '"'), '--benchmark', ('"' + $taskReport + '"'), '--cache-mib', [string]$CacheMiB)
    if ($DecodeMode) { $taskArguments += @('--decode-mode', $DecodeMode, '--decoder-threads', [string]$DecoderThreads) }
    $taskProcess = Start-Process -FilePath $taskApp -ArgumentList $taskArguments -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput (Join-Path $taskReports ($taskName + '-stdout.log')) -RedirectStandardError (Join-Path $taskReports ($taskName + '-stderr.log'))
    if ($taskProcess.ExitCode -ne 0) { throw "Benchmark failed ($($taskProcess.ExitCode)) for $taskSource; see $taskReport" }
    Get-Content -LiteralPath $taskReport
}
