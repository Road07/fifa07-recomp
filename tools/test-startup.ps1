param([ValidateRange(1,300)][int]$DurationSeconds=30, [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Attempt='baseline-1', [ValidateRange(14,256)][int]$ReserveMiB=14, [switch]$ShowWindow, [switch]$TracePhysical, [ValidateSet('on','off')][string]$CubeSharing='on', [ValidateSet('fast','synchronous','disabled')][string]$ReadbackMode='fast', [ValidateSet('refresh','preserve')][string]$GpuPageMode='refresh', [switch]$DumpShaders, [ValidatePattern('^port/cache[a-zA-Z0-9_-]*$')][string]$CacheDirectory='port/cache', [switch]$SynchronousShaders)
$ErrorActionPreference='Stop'
$workspace=Split-Path $PSScriptRoot -Parent
$logDirectory=Join-Path $workspace 'logs'
New-Item -ItemType Directory $logDirectory -Force | Out-Null
$reportPath=Join-Path $logDirectory "$Attempt.json"
if(Test-Path -LiteralPath $reportPath) { throw "Attempt already exists: $Attempt" }
if(Test-Path V:\) { throw 'V: is already in use' }
& subst.exe V: $workspace
if($LASTEXITCODE -ne 0) { throw 'Could not map V:' }
$gameProcess=$null
$previousReserve=$env:FIFA07_PHYSICAL_RESERVE_MIB
$previousTrace=$env:FIFA07_TRACE_PHYSICAL
$previousSharing=$env:FIFA07_SHARE_CUBE_TEXTURES
try {
    $env:FIFA07_PHYSICAL_RESERVE_MIB=[string]$ReserveMiB
    $env:FIFA07_TRACE_PHYSICAL=if($TracePhysical) {'1'} else {'0'}
    $env:FIFA07_SHARE_CUBE_TEXTURES=if($CubeSharing -eq 'on') {'1'} else {'0'}
    $buildDirectory='V:\port\out\build\win-release'
    $arguments=@('--gpu_plugin=xenos','--game_data_root=V:\game','--user_data_root=V:\port\userdata',"--cache_root=V:/$CacheDirectory","--log_file=V:\logs\$Attempt.log",'--headless=true')
    if($SynchronousShaders) { $arguments+='--async_shader_compilation=false' }
    $arguments+=if($ReadbackMode -eq 'disabled') {'--readback_memexport=false'} else {'--readback_memexport=true'}
    $arguments+=if($ReadbackMode -eq 'synchronous') {'--readback_memexport_fast=false'} else {'--readback_memexport_fast=true'}
    $arguments+=if($GpuPageMode -eq 'preserve') {'--clear_memory_page_state=false'} else {'--clear_memory_page_state=true'}
    if($DumpShaders) { $arguments+="--dump_shaders=V:/logs/$Attempt-shaders" }
    $started=Get-Date
    $style=if($ShowWindow) {'Normal'} else {'Hidden'}
    $gameProcess=Start-Process -FilePath "$buildDirectory\fifa07.exe" -ArgumentList $arguments -WorkingDirectory $buildDirectory -WindowStyle $style -PassThru -RedirectStandardOutput (Join-Path $logDirectory "$Attempt.stdout.log") -RedirectStandardError (Join-Path $logDirectory "$Attempt.stderr.log")
    [pscustomobject]@{pid=$gameProcess.Id;started=$started.ToString('o');executable='port/out/build/win-release/fifa07.exe'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $logDirectory "$Attempt-start.json")
    [pscustomobject]@{traceAudio=($env:FIFA07_TRACE_AUDIO -eq '1');xmaFix=($env:FIFA07_STOCK_XMA -ne '1');xmaMode=if($env:FIFA07_STOCK_XMA -eq '1') {'stock'} else {'reference-worker'}} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $logDirectory "$Attempt-audio-options.json")
    [pscustomobject]@{traceCinematics=($env:FIFA07_TRACE_CINEMATICS -eq '1');cacheDirectory=$CacheDirectory;synchronousShaders=$SynchronousShaders.IsPresent} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $logDirectory "$Attempt-cinematic-options.json")
    $exited=$gameProcess.WaitForExit($DurationSeconds*1000)
    $exitCode=if($exited) {$gameProcess.ExitCode} else {$null}
    if(!$exited) { Stop-Process -Id $gameProcess.Id; $gameProcess.WaitForExit() }
    [pscustomobject]@{attempt=$Attempt;traceSkinning=($env:FIFA07_TRACE_SKINNING -eq '1');serialAnimation=($env:FIFA07_SERIAL_ANIMATION -eq '1');poseWriter=($env:FIFA07_TRACE_POSE_WRITER -eq '1');alternatePose=($env:FIFA07_ALTERNATE_POSE -eq '1');auditAnimation=($env:FIFA07_AUDIT_ANIMATION -eq '1');dumpShaders=$DumpShaders.IsPresent;reserveMiB=$ReserveMiB;readbackMode=$ReadbackMode;gpuPageMode=$GpuPageMode;tracePhysical=$TracePhysical.IsPresent;shareCubeTextures=($CubeSharing -eq 'on');durationSeconds=((Get-Date)-$started).TotalSeconds;exitedBeforeTimeout=$exited;exitCode=$exitCode;exeSha256=(Get-FileHash "$buildDirectory\fifa07.exe" -Algorithm SHA256).Hash;runtimeSha256=(Get-FileHash "$buildDirectory\rexruntime.dll" -Algorithm SHA256).Hash;log="logs/$Attempt.log"} | ConvertTo-Json | Set-Content -LiteralPath $reportPath
    Get-Content -LiteralPath $reportPath
    Get-Content -LiteralPath (Join-Path $logDirectory "$Attempt.log") -Tail 25 -ErrorAction SilentlyContinue
} finally {
    $env:FIFA07_PHYSICAL_RESERVE_MIB=$previousReserve
    $env:FIFA07_TRACE_PHYSICAL=$previousTrace
    $env:FIFA07_SHARE_CUBE_TEXTURES=$previousSharing
    if($gameProcess -and !$gameProcess.HasExited) { Stop-Process -Id $gameProcess.Id }
    & subst.exe V: /d
}


