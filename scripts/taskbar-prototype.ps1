param(
    [switch]$Build,
    [switch]$Live,
    [switch]$SelfTest,
    [ValidateRange(1,300)][int]$Seconds = 60,
    [string]$Symbols = '',
    [string]$BuildDirectory = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (!$BuildDirectory) { $BuildDirectory = Join-Path $projectRoot 'build/taskbar-prototype' }
if ($Build) {
    & cmake -S (Join-Path $projectRoot 'src/platform/windows/taskbarprototype') -B $BuildDirectory
    if ($LASTEXITCODE) { throw 'Prototype configure failed' }
    & cmake --build $BuildDirectory --config Release --parallel 4
    if ($LASTEXITCODE) { throw 'Prototype build failed' }
}
$binary = Join-Path $BuildDirectory 'Release/taskbar-prototype.exe'
if (!(Test-Path -LiteralPath $binary)) { $binary = Join-Path $BuildDirectory 'taskbar-prototype.exe' }
if (!(Test-Path -LiteralPath $binary)) { throw 'Build the isolated prototype with -Build first.' }
$prototypeArgs = @('--seconds', "$Seconds")
if ($Live) { $prototypeArgs += '--live' }
if ($SelfTest) { $prototypeArgs += '--self-test' }
if ($Symbols) { $prototypeArgs += @('--symbols', $Symbols) }
& $binary @prototypeArgs
if ($LASTEXITCODE) { throw "Prototype exited with code $LASTEXITCODE" }
