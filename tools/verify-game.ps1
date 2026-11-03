$ErrorActionPreference='Stop'
. "$PSScriptRoot/common.ps1"
$workspace=Split-Path $PSScriptRoot -Parent
$manifest=Get-Content "$PSScriptRoot/game-files.json" -Raw | ConvertFrom-Json
$failed=@()
foreach($entry in $manifest) {
    $path=Join-Path "$workspace/game" $entry.path
    if(!(Test-Path -LiteralPath $path -PathType Leaf)) { $failed+="$($entry.path): missing"; continue }
    if((Get-Item -LiteralPath $path).Length -ne $entry.size) { $failed+="$($entry.path): size mismatch"; continue }
    if((Get-FifaSha256 $path) -ne $entry.sha256) { $failed+="$($entry.path): SHA-256 mismatch" }
}
if($failed.Count) { throw ($failed -join "`n") }
Write-Output "PASS: all $($manifest.Count) game files match the supported disc extraction."
