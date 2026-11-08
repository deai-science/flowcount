$IdfArgs = @($args)
$buildConfig = Join-Path (Split-Path $PSScriptRoot -Parent) '.build-dir'
if((Test-Path -LiteralPath $buildConfig) -and !($IdfArgs -ccontains '-B') -and !($IdfArgs -ccontains '--build-dir')) {
    $configuredBuild = (Get-Content -LiteralPath $buildConfig -Raw).Trim()
    if($configuredBuild) { $IdfArgs = @('-B',$configuredBuild) + $IdfArgs }
}
$ErrorActionPreference = 'Stop'
$idfCandidates = @($env:IDF_PATH, "$env:USERPROFILE\esp\esp-idf-v5.5.5")
$idfRoot = $idfCandidates | Where-Object { $_ -and (Test-Path (Join-Path $_ 'export.ps1')) } | Select-Object -First 1
if (!$idfRoot) { throw 'Install ESP-IDF 5.5.x or set IDF_PATH.' }
$envCandidate = Get-ChildItem "$env:USERPROFILE\.espressif\python_env" -Directory -Filter 'idf5.5_py*_env' |
    Where-Object { Test-Path (Join-Path $_.FullName 'Scripts\python.exe') } | Select-Object -First 1
if (!$envCandidate) { throw 'ESP-IDF 5.5 Python environment not found.' }
$env:IDF_PYTHON_ENV_PATH = $envCandidate.FullName
$env:Path = "$env:IDF_PYTHON_ENV_PATH\Scripts;$env:Path"
. (Join-Path $idfRoot 'export.ps1')
Push-Location (Split-Path $PSScriptRoot -Parent)
try { & python (Join-Path $idfRoot 'tools\idf.py') @IdfArgs; $result = $LASTEXITCODE }
finally { Pop-Location }
exit $result
