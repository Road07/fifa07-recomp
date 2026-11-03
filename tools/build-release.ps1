param([ValidateRange(1,64)][int]$Jobs=8)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/common.ps1"
$workspace=Split-Path $PSScriptRoot -Parent
$python=Get-FifaPython
$vs=Get-FifaVisualStudio
$vcvars=Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$llvm=if($env:FIFA07_LLVM_ROOT) {$env:FIFA07_LLVM_ROOT} else {Join-Path $PSScriptRoot 'llvm-20'}
$compiler=Join-Path $llvm 'bin/clang++.exe'
if(!(Test-Path -LiteralPath $compiler)) {
    $found=Get-Command clang++.exe -ErrorAction SilentlyContinue
    if(!$found) { throw 'Install LLVM/Clang 20, or set FIFA07_LLVM_ROOT to its installation directory.' }
    $compiler=$found.Source
}
$version=& $compiler --version
if($LASTEXITCODE -ne 0 -or ($version -join ' ') -notmatch 'clang version 20\.') { throw 'This project requires Clang 20.x.' }
if($compiler.StartsWith($workspace+'\',[StringComparison]::OrdinalIgnoreCase)) {
    $compiler='V:/'+$compiler.Substring($workspace.Length+1).Replace('\','/')
}
$cmake=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ninja=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
foreach($tool in @($cmake,$ninja,$vcvars)) { if(!(Test-Path -LiteralPath $tool)) { throw "Required VS C++/CMake component is missing: $tool" } }
if(!(Test-Path -LiteralPath "$workspace/game/default.xex")) { throw 'Extract your own FIFA 07 disc to game/ first. See README.md.' }
if((Get-FifaSha256 "$workspace/game/default.xex") -ne '1D5B1F38B3E5238EDF9E68D57A6E8070A71F4296FF203087048D688F02FEFD3A') { throw 'Unsupported default.xex revision. See the supported disc hash in README.md.' }
if(!(Test-Path -LiteralPath "$workspace/tools/rexglue/win-amd64/bin/rexglue.exe")) { throw 'Run tools/setup-dependencies.ps1 first.' }
if(Test-Path V:\) { throw 'V: is already in use; close the running port or free this drive before building.' }
New-Item -ItemType Directory -Path "$workspace/logs" -Force | Out-Null
$env:FIFA07_BUILD_PYTHON=$python
$env:FIFA07_BUILD_COMPILER=$compiler
$env:FIFA07_BUILD_CMAKE=$cmake
$env:FIFA07_BUILD_NINJA=$ninja
$env:FIFA07_BUILD_VCVARS=$vcvars
$env:FIFA07_BUILD_JOBS=[string]$Jobs
$batch=@'
@echo off
call "%FIFA07_BUILD_VCVARS%"
if errorlevel 1 exit /b %errorlevel%
"%FIFA07_BUILD_PYTHON%" V:/tools/fix-vpkd3d-alias.py V:/port/generated/default
if errorlevel 1 exit /b %errorlevel%
"%FIFA07_BUILD_CMAKE%" -S V:/port -B V:/port/out/build/win-release -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_COMPILER=%FIFA07_BUILD_COMPILER%" "-DCMAKE_MAKE_PROGRAM=%FIFA07_BUILD_NINJA%" "-DFIFA07_PATCH_PYTHON=%FIFA07_BUILD_PYTHON%" -DCMAKE_PREFIX_PATH=V:/tools/rexglue/win-amd64
if errorlevel 1 exit /b %errorlevel%
"%FIFA07_BUILD_CMAKE%" --build V:/port/out/build/win-release --parallel %FIFA07_BUILD_JOBS%
exit /b %errorlevel%
'@
$batchPath=Join-Path $workspace 'logs/build-release-environment.cmd'
[IO.File]::WriteAllText($batchPath,$batch.Replace("`r`n","`n").Replace("`n","`r`n"),[Text.UTF8Encoding]::new($false))
& subst.exe V: $workspace
if($LASTEXITCODE -ne 0) { throw 'Could not map V:' }
try {
    & $env:ComSpec /d /c 'V:\logs\build-release-environment.cmd'
    if($LASTEXITCODE -ne 0) { throw "Release build failed: $LASTEXITCODE" }
} finally { & subst.exe V: /d }
