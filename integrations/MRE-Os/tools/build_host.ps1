param(
    [string]$BuildDir = "build-host",
    [ValidateSet("Debug", "Release")][string]$Configuration = "Release"
)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$BuildPath = Join-Path $Root $BuildDir
$Gcc = "C:\msys64\mingw64\bin\gcc.exe"
$Make = "C:\msys64\mingw64\bin\mingw32-make.exe"

if ((Test-Path $Gcc) -and (Test-Path $Make)) {
    cmake -S $Root -B $BuildPath -G "MinGW Makefiles" "-DCMAKE_C_COMPILER=$Gcc" "-DCMAKE_MAKE_PROGRAM=$Make" "-DCMAKE_AR=C:/msys64/mingw64/bin/ar.exe" "-DCMAKE_RANLIB=C:/msys64/mingw64/bin/ranlib.exe" -DMRE_BUILD_HOST_TESTS=ON
} else {
    cmake -S $Root -B $BuildPath -DMRE_BUILD_HOST_TESTS=ON
}
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }

cmake --build $BuildPath --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }

ctest --test-dir $BuildPath --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Host tests failed: $LASTEXITCODE" }
