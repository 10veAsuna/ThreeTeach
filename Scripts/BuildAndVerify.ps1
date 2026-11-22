param(
    [string]$EngineRoot = 'D:\UE_5.8'
)

# 在本机 PowerShell 执行，依次完成 UE 构建、资源导入与自动化测试。
# 先关闭正在使用本项目的 UE 编辑器，让新增反射类型能完整重新加载。
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$project = Join-Path $projectRoot 'GameJam.uproject'
$buildScript = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$output = Join-Path $projectRoot 'Saved\Verification'
New-Item -ItemType Directory -Path $output -Force | Out-Null

foreach ($required in $project, $buildScript, $editor) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required file missing: $required" }
}

& $buildScript GameJamEditor Win64 Development $project -WaitMutex -NoHotReloadFromIDE
if ($LASTEXITCODE -ne 0) { throw "UE build failed with exit code $LASTEXITCODE" }

$importLog = Join-Path $output 'Import.log'
$importScript = Join-Path $PSScriptRoot 'CreateBlueprintAssets.py'
& $editor $project -run=pythonscript "-script=$importScript" -unattended -nop4 -nullrhi -nosound -stdout -FullStdOutLogOutput "-abslog=$importLog" | Out-Host
if ($LASTEXITCODE -ne 0) { throw "Asset import exited with code $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath $importLog) -or
    -not (Select-String -LiteralPath $importLog -Pattern 'ONE_STEP_ASSETS_READY' -Quiet)) {
    throw "Asset import did not complete. See $importLog"
}

$testLog = Join-Path $output 'Automation.log'
& $editor $project -unattended -nop4 -nullrhi -nosound -stdout -FullStdOutLogOutput '-ExecCmds=Automation RunTests OneStep' '-TestExit=Automation Test Queue Empty' "-abslog=$testLog" | Out-Host
if ($LASTEXITCODE -ne 0) { throw "Automation exited with code $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath $testLog)) { throw 'Automation log was not created' }

$successes = @(Select-String -LiteralPath $testLog -Pattern 'Test Completed\. Result=\{Success\}.*OneStep\.')
$failures = @(Select-String -LiteralPath $testLog -Pattern 'Test Completed\. Result=\{Fail|Fatal error:|Assertion failed:')
$requiredTests = @('OneStep.Rules.Fusion', 'OneStep.Runtime.ContinuousBoard')
foreach ($test in $requiredTests) {
    if (-not ($successes | Where-Object { $_.Line.Contains($test) })) {
        throw "Required test did not succeed: $test. See $testLog"
    }
}
if ($failures.Count -gt 0 -or $successes.Count -lt 9) {
    throw "Expected at least nine successful tests. See $testLog"
}

[pscustomobject]@{
    Success = $true
    Tests = $successes.Count
    Chapters = 5
    CampaignBoards = 5
    CompletedAt = (Get-Date).ToString('o')
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'Result.json') -Encoding utf8
Write-Host 'Build, chapter import and automation tests completed successfully.'
