param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [string]$QtRoot = '',
    [ValidateRange(1, 64)][int]$Jobs = 4,
    [switch]$SkipTests,
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
$env:VSLANG = '1033'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $QtRoot) {
    if ($env:QT_ROOT_DIR) { $QtRoot = $env:QT_ROOT_DIR }
    else { $QtRoot = Join-Path $projectRoot '.tools\Qt\6.8.3\msvc2022_64' }
}
if (-not (Test-Path -LiteralPath (Join-Path $QtRoot 'lib\cmake\Qt6\Qt6Config.cmake'))) {
    throw "Qt 6.8+ не знайдено. Запустіть scripts\setup.ps1 або задайте -QtRoot."
}
$localTools = Join-Path $projectRoot '.tools\python\Scripts'
$cmake = if (Test-Path "$localTools\cmake.exe") { "$localTools\cmake.exe" } else { (Get-Command cmake -ErrorAction Stop).Source }
$ninja = if (Test-Path "$localTools\ninja.exe") { "$localTools\ninja.exe" } else { (Get-Command ninja -ErrorAction Stop).Source }
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vsCandidates = @(
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat',
        'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
    )
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vsInstall) { $vsCandidates = @((Join-Path $vsInstall 'VC\Auxiliary\Build\vcvars64.bat')) + $vsCandidates }
    }
    $vcvars = $vsCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $vcvars) { throw 'Потрібні Visual Studio Build Tools із Desktop development with C++.' }
    # Import the compiler environment. No filesystem operations are delegated to cmd.
    $compilerEnvironment = & $env:ComSpec /d /c "call `"$vcvars`" >nul 2>&1 && set"
    if ($LASTEXITCODE -ne 0) { throw 'Не вдалося налаштувати середовище MSVC.' }
    foreach ($line in $compilerEnvironment) {
        if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
    }
}
$env:VSLANG = '1033'
$env:PATH = (Join-Path $QtRoot 'bin') + ';' + $env:PATH
$buildDir = Join-Path $projectRoot "build\$Configuration"
& $cmake -S $projectRoot -B $buildDir -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_PREFIX_PATH=$QtRoot" "-DCMAKE_BUILD_TYPE=$Configuration" '-DBUILD_TESTING=ON'
if ($LASTEXITCODE -ne 0) { throw 'Помилка конфігурації CMake.' }
& $cmake --build $buildDir --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Помилка збирання.' }
if (-not $SkipTests) {
    & (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir $buildDir --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Тести не пройшли.' }
    & (Join-Path $buildDir 'IslandDesktop.exe') --settings (Join-Path $buildDir 'smoke-settings.ini') --smoke-test
    if ($LASTEXITCODE -ne 0) { throw 'Не вдалося завантажити інтерфейс QML.' }
}
if ($Package) {
    $destination = Join-Path $projectRoot 'dist\IslandDesktop'
    # A running portable copy locks its exe and DLLs; close it for the copy and reopen it afterwards.
    $portableExe = Join-Path $destination 'IslandDesktop.exe'
    $running = @(Get-Process -Name IslandDesktop -ErrorAction SilentlyContinue | Where-Object { -not $_.Path -or $_.Path -eq $portableExe })
    if ($running.Count -gt 0) {
        Write-Host 'Закриваю запущену portable-копію на час оновлення…'
        try { $running | Stop-Process -Force -ErrorAction Stop }
        catch { throw 'Не вдалося закрити запущений Island Desktop (можливо, він запущений від адміністратора). Закрийте його через трей і повторіть.' }
        $running | Wait-Process -Timeout 10 -ErrorAction SilentlyContinue
    }
    & $cmake --install $buildDir --prefix $destination
    if ($LASTEXITCODE -ne 0) { throw 'Помилка підготовки portable-збірки.' }
    Write-Host "Portable: $portableExe"
    if ($running.Count -gt 0) {
        Start-Process -FilePath $portableExe -WorkingDirectory $destination -WindowStyle Hidden
        Write-Host 'Оновлену копію запущено знову.'
    }
} else {
    Write-Host "Готово: $buildDir\IslandDesktop.exe"
    Write-Host 'Для запуску: .\scripts\run.ps1'
}
