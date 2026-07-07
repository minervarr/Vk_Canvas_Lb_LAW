$ErrorActionPreference = 'Stop'

$root  = $PSScriptRoot
$build = Join-Path $root 'build'
$ninja = 'C:\PPProgam\ninja_win\bin\ninja.exe'

# ── Auto-detect vcvars64.bat (which sets up the cl.exe path) ────────────────
$vcvars = $null
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

if (Test-Path $vswhere) {
    # Ask vswhere to find the latest VS installation that has C++ desktop tools
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
    if ($vsPath) {
        $candidate = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
        if (Test-Path $candidate) {
            $vcvars = $candidate
        }
    }
}

# Fallback if vswhere didn't find it (e.g., older VS or missing vswhere)
if (-not $vcvars) {
    $vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
}

# Final check to ensure we actually found the file
if (-not (Test-Path $vcvars)) {
    throw "Could not find vcvars64.bat. Please ensure Visual Studio (or Build Tools) is installed with the 'Desktop development with C++' workload."
}

Write-Host '[1/3] Setting up MSVC x64 environment...' -ForegroundColor Cyan
Write-Host "Using: $vcvars" -ForegroundColor DarkGray

$envDump = cmd /c "`"$vcvars`" > nul && set"
foreach ($line in $envDump) {
    if ($line -match '^([^=]+)=(.*)$') {
        [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}

# ── CMake Configure ─────────────────────────────────────────────────────────
Write-Host '[2/3] Configuring...' -ForegroundColor Cyan
if (-not (Test-Path $build)) { New-Item -ItemType Directory $build | Out-Null }

& cmake -S $root -B $build -G Ninja `
    -DCMAKE_MAKE_PROGRAM="$ninja" `
    -DCMAKE_C_COMPILER=cl `
    -DCMAKE_CXX_COMPILER=cl `
    -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }

# ── CMake Build ─────────────────────────────────────────────────────────────
Write-Host "[3/3] Building ($env:NUMBER_OF_PROCESSORS cores)..." -ForegroundColor Cyan
& cmake --build $build -j $env:NUMBER_OF_PROCESSORS
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }

Write-Host "`nDone: $build\vk_canvas.exe" -ForegroundColor Green
