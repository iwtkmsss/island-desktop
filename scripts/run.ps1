param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release', [switch]$Expanded)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$portable = Join-Path $projectRoot 'dist\IslandDesktop\IslandDesktop.exe'
$development = Join-Path $projectRoot "build\$Configuration\IslandDesktop.exe"
if (Test-Path -LiteralPath $portable) { $executable = $portable }
elseif (Test-Path -LiteralPath $development) {
    $qtRoot = if ($env:QT_ROOT_DIR) { $env:QT_ROOT_DIR } else { Join-Path $projectRoot '.tools\Qt\6.8.3\msvc2022_64' }
    $env:PATH = (Join-Path $qtRoot 'bin') + ';' + $env:PATH
    $executable = $development
} else { throw 'Спочатку запустіть .\scripts\build.ps1 -Package' }
$launchOptions = @{ FilePath = $executable; WindowStyle = 'Hidden'; WorkingDirectory = (Split-Path $executable) }
if ($Expanded) { $launchOptions.ArgumentList = '--expanded' }
Start-Process @launchOptions
