param([switch]$SourceOnly, [string]$SdkArchive)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/common.ps1"
$workspace=Split-Path $PSScriptRoot -Parent
$lock=Get-Content "$PSScriptRoot/dependencies.lock.json" -Raw | ConvertFrom-Json
function Invoke-GitChecked([string[]]$GitArguments) {
    & git @GitArguments
    if($LASTEXITCODE -ne 0) { throw "Git failed: $($GitArguments -join ' ')" }
}
if(!(Get-Command git -ErrorAction SilentlyContinue)) { throw 'Install Git for Windows first.' }
$sdkSource=Join-Path $PSScriptRoot 'rexglue-source'
if(!(Test-Path -LiteralPath $sdkSource)) {
    Invoke-GitChecked @('clone','--filter=blob:none','--no-checkout',$lock.sdk.repository,$sdkSource)
    Invoke-GitChecked @('-C',$sdkSource,'checkout','--detach',$lock.sdk.commit)
}
$head=& git -C $sdkSource rev-parse HEAD
if($LASTEXITCODE -ne 0 -or $head -ne $lock.sdk.commit) { throw 'Existing SDK source differs from the pinned commit; it was not modified. Use a separate checkout.' }
Invoke-GitChecked @('-C',$sdkSource,'submodule','update','--init','--depth','1','thirdparty/FFmpeg')
$ffmpegHead=& git -C "$sdkSource/thirdparty/FFmpeg" rev-parse HEAD
if($LASTEXITCODE -ne 0 -or $ffmpegHead -ne $lock.ffmpeg.commit) { throw 'FFmpeg submodule does not match the pinned SDK ABI.' }
if(!$SourceOnly) {
    $sdkDirectory=Join-Path $PSScriptRoot 'rexglue'
    $sdkExe=Join-Path $sdkDirectory 'win-amd64/bin/rexglue.exe'
    if(!(Test-Path -LiteralPath $sdkExe)) {
        New-Item -ItemType Directory -Path "$PSScriptRoot/downloads" -Force | Out-Null
        $archive=if($SdkArchive) {(Resolve-Path -LiteralPath $SdkArchive).Path} else {Join-Path $PSScriptRoot 'downloads/rexglue-win-amd64.zip'}
        if((Test-Path -LiteralPath $archive) -and (Get-FifaSha256 $archive) -ne $lock.sdk.windows_sha256) {
            if($SdkArchive) { throw 'Provided SDK archive SHA-256 does not match dependencies.lock.json.' }
            Move-Item -LiteralPath $archive -Destination ($archive+'.invalid-'+[DateTime]::UtcNow.ToString('yyyyMMddHHmmssfff'))
        }
        if(!(Test-Path -LiteralPath $archive)) {
            [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
            $partial=$archive+'.partial'
            for($attempt=1;$attempt -le 3;$attempt++) {
                try {
                    if(Get-Command curl.exe -ErrorAction SilentlyContinue) {
                        & curl.exe --fail --location --silent --show-error --connect-timeout 15 --max-time 120 --output $partial $lock.sdk.windows_url
                        if($LASTEXITCODE -ne 0) { throw "SDK download failed: curl exit $LASTEXITCODE" }
                    } else {
                        Invoke-WebRequest -UseBasicParsing -TimeoutSec 120 -Uri $lock.sdk.windows_url -OutFile $partial
                    }
                    if((Get-FifaSha256 $partial) -ne $lock.sdk.windows_sha256) { throw 'Downloaded SDK checksum mismatch.' }
                    Move-Item -LiteralPath $partial -Destination $archive
                    break
                } catch {
                    if($attempt -eq 3) { throw }
                    Write-Warning "SDK download attempt $attempt failed; retrying."
                    Start-Sleep -Seconds 2
                }
            }
        }
        if((Get-FifaSha256 $archive) -ne $lock.sdk.windows_sha256) { throw 'SDK archive SHA-256 mismatch; archive was not extracted.' }
        if(Test-Path -LiteralPath "$sdkDirectory/win-amd64") { throw 'Incomplete SDK directory already exists; it was not overwritten.' }
        New-Item -ItemType Directory -Path $sdkDirectory -Force | Out-Null
        Expand-Archive -LiteralPath $archive -DestinationPath $sdkDirectory
    }
    $version=& $sdkExe --version
    if($LASTEXITCODE -ne 0 -or ($version -join ' ') -notlike "*$($lock.sdk.version)*") { throw 'Installed SDK executable version differs from the pinned release.' }
}
Write-Output 'Pinned SDK source and FFmpeg ABI are ready.'
