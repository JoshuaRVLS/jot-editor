<#
build.ps1 - one-command rebuild for jot on Windows.

Dependencies are self-fetched (libuv / Lua / utf8proc build from source via
FetchContent when JOT_FETCH_DEPS=ON, which is the default), so the only
requirements are CMake and a Visual Studio / MSVC toolchain.

Examples:
  ./build.ps1                 configure + build Release
  ./build.ps1 -Run            build, then launch jot
  ./build.ps1 -Pull           git pull origin main first, then build
  ./build.ps1 -Config Debug   build a Debug binary
  ./build.ps1 -Clean          wipe the build directory and reconfigure fresh
  ./build.ps1 -Tests          also build and run the CTest suite
  ./build.ps1 -InputLog       launch jot with the win32 input log enabled
                              (writes to %TEMP%\jot-input.log; use for
                              diagnosing key/mouse delivery issues)

  Flags combine: ./build.ps1 -Pull -Run
#>
[CmdletBinding()]
param(
  [switch]$Pull,
  [switch]$Run,
  [switch]$Clean,
  [switch]$Tests,
  [switch]$InputLog,
  [ValidateSet('Release', 'Debug')]
  [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

function Step($msg) { Write-Host "`n==> $msg" -ForegroundColor Cyan }
function Ok($msg)   { Write-Host "    $msg" -ForegroundColor Green }
function Warn($msg) { Write-Host "    $msg" -ForegroundColor Yellow }
function Fail($msg) { Write-Host "    $msg" -ForegroundColor Red; exit 1 }

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
  Fail 'cmake was not found. Install CMake (winget install Kitware.CMake) and re-run.'
}

if ($Pull) {
  Step 'Pulling latest main'
  if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Fail 'git was not found.'
  }
  Push-Location $root
  try {
    git fetch origin main
    git pull --ff-only origin main
  }
  finally { Pop-Location }
  Ok 'working tree updated'
}

$buildDir = Join-Path $root 'build'
if ($Clean -and (Test-Path $buildDir)) {
  Step 'Cleaning build directory'
  Remove-Item -Recurse -Force $buildDir
  Ok 'build/ removed'
}

Step 'Configuring CMake (VS x64, deps fetched from source)'
$configureArgs = @('-S', $root, '-B', $buildDir, '-A', 'x64', '-DJOT_TREESITTER=OFF')
$configureArgs += if ($Tests) { '-DBUILD_TESTING=ON' } else { '-DBUILD_TESTING=OFF' }
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { Fail 'CMake configure failed.' }
Ok 'configured'

Step "Building $Config"
& cmake --build $buildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) { Fail 'Build failed.' }
Ok 'build finished'

# The Visual Studio generator nests the exe under apps/jot/<Config>.
$exe = Join-Path $buildDir (Join-Path "apps\jot\$Config" 'jot.exe')
if (-not (Test-Path $exe)) {
  $found = Get-ChildItem -Path $buildDir -Recurse -Filter 'jot.exe' -ErrorAction SilentlyContinue |
    Select-Object -First 1
  if (-not $found) { Fail 'Could not locate jot.exe in the build directory.' }
  $exe = $found.FullName
}
Ok "binary: $exe"

if ($Tests) {
  Step 'Running tests'
  & ctest --test-dir $buildDir -C $Config --output-on-failure
  if ($LASTEXITCODE -ne 0) { Fail 'Some tests failed.' }
  Ok 'tests passed'
}

if ($Run) {
  Step 'Launching jot'
  if ($InputLog) {
    $log = Join-Path $env:TEMP 'jot-input.log'
    Warn "JOT_WIN32_INPUT_LOG=$log (delete it between runs to keep it readable)"
    $env:JOT_WIN32_INPUT_LOG = $log
  }
  & $exe
  exit $LASTEXITCODE
}

Write-Host ''
Ok 'Done. Run jot with:'
Ok "  & `"$exe`""
