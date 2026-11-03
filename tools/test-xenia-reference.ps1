param([ValidateRange(10,300)][int]$DurationSeconds=180, [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Attempt='xenia-reference-trial', [ValidateSet('new','old','master')][string]$XmaDecoder='new')
$ErrorActionPreference='Stop'
$workspace=Split-Path $PSScriptRoot -Parent
if(Test-Path V:\) { throw 'V: is already in use' }
$report=Join-Path $workspace "logs/$Attempt.json"
if(Test-Path -LiteralPath $report) { throw 'Reference trial already exists' }
& subst.exe V: $workspace
if($LASTEXITCODE -ne 0) { throw 'Could not map V:' }
$referenceProcess=$null
try {
    Set-Content -LiteralPath 'V:/tools/xenia-reference/portable.txt' -Value ''
    $arguments=@('V:/game/default.xex','--gpu=d3d12','--vsync=true',"--xma_decoder=$XmaDecoder","--log_file=V:/logs/$Attempt.log")
    $started=Get-Date
    $referenceProcess=Start-Process -FilePath 'V:/tools/xenia-reference/xenia_canary.exe' -ArgumentList $arguments -WorkingDirectory 'V:/tools/xenia-reference' -WindowStyle Normal -PassThru
    [pscustomobject]@{pid=$referenceProcess.Id;started=$started.ToString('o');executable='tools/xenia-reference/xenia_canary.exe'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $workspace "logs/$Attempt-start.json")
    $exited=$referenceProcess.WaitForExit($DurationSeconds*1000)
    $code=if($exited) {$referenceProcess.ExitCode} else {$null}
    if(!$exited) { Stop-Process -Id $referenceProcess.Id; $referenceProcess.WaitForExit() }
    [pscustomobject]@{xmaDecoder=$XmaDecoder;durationSeconds=((Get-Date)-$started).TotalSeconds;exitedBeforeTimeout=$exited;exitCode=$code;exeSha256=(Get-FileHash 'V:/tools/xenia-reference/xenia_canary.exe').Hash;gameSha256=(Get-FileHash 'V:/game/default.xex').Hash;log="logs/$Attempt.log"} | ConvertTo-Json | Set-Content -LiteralPath $report
    Get-Content -LiteralPath $report
} finally {
    if($referenceProcess -and !$referenceProcess.HasExited) { Stop-Process -Id $referenceProcess.Id }
    & subst.exe V: /d
}
