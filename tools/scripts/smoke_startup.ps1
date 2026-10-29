param([switch]$GraphicsTest, [switch]$ThreeQuarterScale, [switch]$CaptureTitle, [switch]$Autoplay, [ValidateRange(50,400)][uint32]$InternalScalePercent=100, [ValidateRange(1,120)][int]$Seconds=15, [ValidateSet('native','story_candidate')][string]$BuildName='native')
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path "$PSScriptRoot/../..").Path
$runDirectory = Join-Path $taskRoot 'analysis/runs'
New-Item -ItemType Directory -Force -Path $runDirectory | Out-Null
$existing = Get-ChildItem -LiteralPath $runDirectory -Filter '*.log'
$sequence = 1 + (($existing | ForEach-Object { [int]$_.BaseName } | Measure-Object -Maximum).Maximum)
$recordName = '{0:D3}' -f [int]$sequence
$nativeDirectory = Join-Path $taskRoot "build/$BuildName"
$logPath = Join-Path $nativeDirectory 'logs/tenchu_startup.log'
if (Test-Path -LiteralPath $logPath) { Remove-Item -LiteralPath $logPath }
if ($GraphicsTest) { $env:TENCHU_GPU_SELFTEST = '1' }
if ($ThreeQuarterScale) { $env:TENCHU_GPU_THREE_QUARTER = '1' }
if ($GraphicsTest -and $PSBoundParameters.ContainsKey('InternalScalePercent')) { $env:TENCHU_TEST_INTERNAL_PERCENT = [string]$InternalScalePercent }
if ($CaptureTitle) { $env:TENCHU_CAPTURE_TITLE = '1' }
if ($Autoplay) { $env:TENCHU_AUTOPLAY = '1' }
try {
    $process = Start-Process -FilePath (Join-Path $nativeDirectory 'TenchuRFD.exe') -WorkingDirectory $nativeDirectory -WindowStyle Hidden -PassThru
} finally {
    if ($GraphicsTest) { Remove-Item Env:TENCHU_GPU_SELFTEST -ErrorAction SilentlyContinue }
    if ($ThreeQuarterScale) { Remove-Item Env:TENCHU_GPU_THREE_QUARTER -ErrorAction SilentlyContinue }
    Remove-Item Env:TENCHU_TEST_INTERNAL_PERCENT -ErrorAction SilentlyContinue
    if ($CaptureTitle) { Remove-Item Env:TENCHU_CAPTURE_TITLE -ErrorAction SilentlyContinue }
    if ($Autoplay) { Remove-Item Env:TENCHU_AUTOPLAY -ErrorAction SilentlyContinue }
}
$finished = $process.WaitForExit($Seconds*1000)
if (-not $finished) {
    Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
    $process.WaitForExit()
}
[pscustomobject]@{ kind=$(if($GraphicsTest){'graphics_selftest'}else{'startup'}); build=$BuildName; capture_title=[bool]$CaptureTitle; exited_before_timeout=$finished; exit_code=$process.ExitCode; timeout_seconds=$Seconds } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDirectory "$recordName.json")
Copy-Item -LiteralPath $logPath -Destination (Join-Path $runDirectory "$recordName.log")
Write-Output "Saved run $recordName"
Get-Content -LiteralPath $logPath | Select-String -Pattern 'error:|trace|D3D|Swap|frame' | Select-Object -First 10
if ($GraphicsTest -and (-not $finished -or $process.ExitCode -ne 0)) { exit 1 }
if ($GraphicsTest -and $PSBoundParameters.ContainsKey('InternalScalePercent')) {
    $expectedDimension=[int][math]::Round(64*$InternalScalePercent/100)
    $expected="GPU target allocation: 64x64 -> ${expectedDimension}x${expectedDimension}"
    if (!(Select-String -LiteralPath $logPath -SimpleMatch $expected -Quiet)) { throw "GPU allocation did not match selected resolution: $expected" }
}
