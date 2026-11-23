param(
    [Parameter(Mandatory=$true)][string]$LinkAddress,
    [string]$BuildDir = "build-aarch64",
    [string]$Compiler = "C:\msys64\mingw64\bin\clang.exe"
)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path $Compiler)) { throw "Clang not found: $Compiler. Install the MSYS2 x86_64 Clang package first." }
$BuildPath = Join-Path $Root $BuildDir
$CMakeArgs = @(
    "-S", $Root, "-B", $BuildPath, "-G", "MinGW Makefiles",
    "-DCMAKE_C_COMPILER=$Compiler",
    "-DCMAKE_SYSTEM_NAME=Generic",
    "-DCMAKE_SYSTEM_PROCESSOR=aarch64",
    "-DCMAKE_C_COMPILER_TARGET=aarch64-none-elf",
    "-DCMAKE_ASM_COMPILER=$Compiler",
    "-DCMAKE_ASM_COMPILER_TARGET=aarch64-none-elf",
    "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
    "-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld",
    "-DCMAKE_AR=C:/msys64/mingw64/bin/llvm-ar.exe",
    "-DCMAKE_RANLIB=C:/msys64/mingw64/bin/llvm-ranlib.exe",
    "-DCMAKE_LINKER=C:/msys64/mingw64/bin/ld.lld.exe",
    "-DMRE_BUILD_HOST_TESTS=OFF",
    "-DMRE_BUILD_AARCH64_KERNEL=ON",
    "-DMRE_KERNEL_LINK_ADDRESS=$LinkAddress"
)
& cmake @CMakeArgs
if ($LASTEXITCODE -ne 0) { throw "AArch64 CMake configure failed: $LASTEXITCODE" }
cmake --build $BuildPath --config Release
if ($LASTEXITCODE -ne 0) { throw "AArch64 kernel build failed: $LASTEXITCODE" }
Write-Host "AArch64 ELF built. This does not create or flash an Android boot image."
