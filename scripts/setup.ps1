param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$pythonEnv = Join-Path $projectRoot '.tools\python'
if (-not (Test-Path -LiteralPath "$pythonEnv\Scripts\python.exe")) {
    & $Python -m venv $pythonEnv
    if ($LASTEXITCODE -ne 0) { throw 'Не вдалося створити Python venv.' }
}
$localPython = Join-Path $pythonEnv 'Scripts\python.exe'
& $localPython -m pip install 'aqtinstall==3.3.0' 'cmake==4.4.4' 'ninja==1.13.2'
if ($LASTEXITCODE -ne 0) { throw 'Помилка встановлення інструментів.' }
$qtRoot = Join-Path $projectRoot '.tools\Qt'
if (-not (Test-Path -LiteralPath "$qtRoot\6.8.3\msvc2022_64\bin\qmake.exe")) {
    & $localPython -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 --outputdir $qtRoot --archives qtbase qtdeclarative qtsvg qttools qttranslations d3dcompiler_47 --modules qtshadertools
    if ($LASTEXITCODE -ne 0) { throw 'Помилка встановлення Qt.' }
}
Write-Host 'Готово. Тепер запустіть .\scripts\build.ps1 -Package'
