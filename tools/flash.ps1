param([Parameter(Mandatory=$true)][string]$Backup,[string]$Port='COM5')
$ErrorActionPreference='Stop'
$pythonEnv=Get-ChildItem "$env:USERPROFILE\.espressif\python_env" -Directory -Filter 'idf5.5_py*_env' | Select-Object -First 1
if(!$pythonEnv) { throw 'ESP-IDF 5.5 Python environment not found.' }
& (Join-Path $pythonEnv.FullName 'Scripts\python.exe') (Join-Path $PSScriptRoot 'verify_backup.py') $Backup
if($LASTEXITCODE -ne 0) { throw 'A complete, verified backup is required.' }
& (Join-Path $PSScriptRoot 'idf.ps1') build
if($LASTEXITCODE -ne 0) { throw 'Build failed. The board has not been erased.' }
# Erases the P4 flash, then writes the application. Close the USB monitor first.
& (Join-Path $PSScriptRoot 'idf.ps1') -p $Port -b 460800 erase-flash flash
exit $LASTEXITCODE
