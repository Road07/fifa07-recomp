function Get-FifaSha256([string]$Path) {
    $stream=[IO.File]::OpenRead($Path)
    $hash=[Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($hash.ComputeHash($stream))).Replace('-','').ToLowerInvariant() }
    finally { $stream.Dispose(); $hash.Dispose() }
}

function Get-FifaPython {
    if($env:FIFA07_PYTHON) {
        if(!(Test-Path -LiteralPath $env:FIFA07_PYTHON -PathType Leaf)) { throw 'FIFA07_PYTHON must point to python.exe' }
        return (Resolve-Path -LiteralPath $env:FIFA07_PYTHON).Path
    }
    $pythonCommand=Get-Command python.exe -All -ErrorAction SilentlyContinue |
        Where-Object {$_.Source -notlike '*\Microsoft\WindowsApps\*'} | Select-Object -First 1
    if($pythonCommand) { return $pythonCommand.Source }
    throw 'Install Python 3 and add it to PATH, or set FIFA07_PYTHON to its python.exe path.'
}

function Get-FifaVisualStudio {
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if(!(Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio 2022 C++ Build Tools.' }
    $installation=& $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if($LASTEXITCODE -ne 0 -or !$installation) { throw 'Visual Studio 2022 with C++ tools was not found.' }
    return [string]$installation
}
