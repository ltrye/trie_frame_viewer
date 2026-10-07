param([switch]$Native, [string]$AppPath = '')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskArtifacts = Join-Path $taskRoot 'artifacts'
New-Item -ItemType Directory -Force -Path $taskArtifacts | Out-Null
$taskFixture = Join-Path $taskArtifacts 'reference-test.mp4'
& ffmpeg -hide_banner -loglevel error -f lavfi -i 'testsrc2=size=640x360:rate=24' -f lavfi -i 'sine=frequency=880:sample_rate=48000' -t 4 -c:v libx264 -g 48 -pix_fmt yuv420p -c:a aac -y $taskFixture
if ($LASTEXITCODE -ne 0) { throw 'Could not generate media fixture' }
$env:TFV_TEST_MEDIA = $taskFixture
$taskStressFixture = Join-Path $taskArtifacts 'seek-stress.mp4'
& ffmpeg -hide_banner -loglevel error -f lavfi -i 'testsrc2=size=1920x1080:rate=30' -f lavfi -i 'sine=frequency=440:sample_rate=48000' -t 12 -c:v libx264 -preset fast -g 240 -keyint_min 240 -sc_threshold 0 -bf 3 -pix_fmt yuv420p -c:a aac -y $taskStressFixture
if ($LASTEXITCODE -ne 0) { throw 'Could not generate seek stress fixture' }
$env:TFV_STRESS_MEDIA = $taskStressFixture
& python (Join-Path $PSScriptRoot 'dev.py') test
if ($LASTEXITCODE -ne 0) { throw 'Core tests failed' }
$taskApp = if ($AppPath) { (Resolve-Path -LiteralPath $AppPath).Path } else { Join-Path $taskRoot 'out\TrieFrameViewer\TrieFrameViewer.exe' }
if (-not (Test-Path -LiteralPath $taskApp)) { throw 'Deploy first: python scripts\dev.py deploy' }
$taskArguments = @('"' + $taskFixture + '"', '--smoke-test', '"' + $taskArtifacts + '"')
if (-not $Native) {
    $env:QT_QUICK_BACKEND = 'software'
    $env:QT_QPA_FONTDIR = Join-Path $env:WINDIR 'Fonts'
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $taskRoot '.tools\Qt\6.8.3\msvc2022_64\plugins\platforms'
    $taskArguments = @('-platform','offscreen') + $taskArguments
}
$taskProcess = Start-Process -FilePath $taskApp -ArgumentList $taskArguments -WindowStyle Hidden -RedirectStandardOutput (Join-Path $taskArtifacts 'app-stdout.log') -RedirectStandardError (Join-Path $taskArtifacts 'app-stderr.log') -PassThru -Wait
if ($taskProcess.ExitCode -ne 0) { throw "App smoke test failed: $($taskProcess.ExitCode)" }
Write-Output "Verified. Screenshots and report: $taskArtifacts"
