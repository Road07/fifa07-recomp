param([switch]$FixPackAlias)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/common.ps1"
$python=Get-FifaPython
$workspace=Split-Path $PSScriptRoot -Parent
if(Test-Path V:\) {throw 'V: is already in use'}
& subst.exe V: $workspace
if($LASTEXITCODE -ne 0) {throw 'Could not map V:'}
try {
  $root='V:\logs\vmx'
  foreach($directory in @('asm','obj','bin','generated')) {
    New-Item -ItemType Directory (Join-Path $root $directory) -Force | Out-Null
  }
  $binutils='V:\tools\rexglue-source\tools\binutils'
  $sources=@(Get-ChildItem 'V:\tools\rexglue-source\tests\ppc\asm' -File | Where-Object {$_.Name -match '^instr_(v|lv|stv)'})
  $sources+=Get-Item 'V:/tools/vmx_harness/instr_vpkd3d_alias.s'
  foreach($source in $sources) {
    Copy-Item -LiteralPath $source.FullName -Destination (Join-Path "$root\asm" $source.Name)
  }
  function Build-PpcTestFile($source) {
    $stem=$source.BaseName
    & "$binutils\powerpc-none-elf-as.exe" -a32 -be -mregnames -mpower7 -maltivec -mvsx -mvmx128 -R -o "/cygdrive/v/logs/vmx/obj/$stem.o" "/cygdrive/v/logs/vmx/asm/$($source.Name)"
    if($LASTEXITCODE -ne 0) {throw "Assembly failed: $stem"}
    & "$binutils\powerpc-none-elf-ld.exe" -A powerpc:common32 -melf32ppc -EB -nostdlib --oformat=binary -Ttext=0x82010000 -e 0x82010000 -o "/cygdrive/v/logs/vmx/bin/$stem.bin" "/cygdrive/v/logs/vmx/obj/$stem.o"
    if($LASTEXITCODE -ne 0) {throw "Link failed: $stem"}
    & "$binutils\powerpc-none-elf-nm.exe" --numeric-sort "/cygdrive/v/logs/vmx/obj/$stem.o" | Set-Content -Encoding ascii "$root\bin\$stem.map"
    if($LASTEXITCODE -ne 0) {throw "Symbol extraction failed: $stem"}
  }
  foreach($source in $sources) { Build-PpcTestFile $source }
  & $python 'V:/tools/generate-vmx-high-tests.py'
  if($LASTEXITCODE -ne 0) {throw 'High-register test generation failed'}
  Build-PpcTestFile (Get-Item "$root\asm\instr_vmx_high_registers.s")
  Write-Output "Assembled $($sources.Count) official vector test files"
  & 'V:\tools\rexglue\win-amd64\bin\rexglue.exe' recompile-tests --bin-dir V:/logs/vmx/bin --asm-dir V:/logs/vmx/asm --output V:/logs/vmx/generated
  if($LASTEXITCODE -ne 0) {throw 'Vector test codegen failed'}
  if($FixPackAlias) {
    & $python 'V:/tools/fix-vpkd3d-alias.py' 'V:/logs/vmx/generated'
    if($LASTEXITCODE -ne 0) {throw 'Float pack alias correction failed'}
  }
  & 'V:\tools\test-vmx-build.cmd'
  $testResult=$LASTEXITCODE
  if($testResult -ne 0) {throw "Vector regression failed: $testResult"}
} finally {
  & subst.exe V: /d
}
