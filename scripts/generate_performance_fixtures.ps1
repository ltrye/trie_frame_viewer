param([switch]$Regenerate)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskArtifacts = Join-Path $taskRoot 'artifacts'
New-Item -ItemType Directory -Force -Path $taskArtifacts | Out-Null
$taskFixtures = @(
    @{Name='reference-test'; Size='640x360'; Rate=24; Frames=96; Gop=48; Audio=$true},
    @{Name='reference-720'; Size='1280x720'; Rate=30; Frames=271; Gop=240; Audio=$false},
    @{Name='seek-stress'; Size='1920x1080'; Rate=30; Frames=360; Gop=240; Audio=$true},
    @{Name='long-4000'; Size='1280x720'; Rate=30; Frames=4000; Gop=240; Audio=$false}
)
foreach ($taskFixture in $taskFixtures) {
    $taskOutput = Join-Path $taskArtifacts ($taskFixture.Name + '.mp4')
    if ((Test-Path -LiteralPath $taskOutput) -and -not $Regenerate) { continue }
    $taskArguments = @('-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i', "testsrc2=size=$($taskFixture.Size):rate=$($taskFixture.Rate)")
    if ($taskFixture.Audio) { $taskArguments += @('-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000', '-c:a', 'aac', '-t', [string]($taskFixture.Frames / $taskFixture.Rate)) }
    else { $taskArguments += '-an' }
    $taskArguments += @('-frames:v', [string]$taskFixture.Frames, '-c:v', 'libx264', '-threads', '4', '-preset', 'veryfast', '-crf', '23', '-g', [string]$taskFixture.Gop, '-keyint_min', [string]$taskFixture.Gop, '-sc_threshold', '0', '-bf', '3', '-pix_fmt', 'yuv420p', '-y', $taskOutput)
    & ffmpeg @taskArguments
    if ($LASTEXITCODE -ne 0) { throw "Could not generate $taskOutput" }
}
