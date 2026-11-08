param([string]$Port='COM5', [switch]$Reset)
$ErrorActionPreference='Stop'
$pythonEnv=Get-ChildItem "$env:USERPROFILE\.espressif\python_env" -Directory -Filter 'idf5.5_py*_env' | Select-Object -First 1
if(!$pythonEnv) { throw 'ESP-IDF 5.5 Python environment not found.' }
$arguments=@((Join-Path $PSScriptRoot 'monitor.py'),'--port',$Port)
if($Reset) { $arguments+='--reset' }
& (Join-Path $pythonEnv.FullName 'Scripts\python.exe') @arguments
