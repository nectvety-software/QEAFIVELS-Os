$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Compiler = "C:\msys64\mingw64\bin\clang.exe"
if (-not (Test-Path $Compiler)) { throw "AArch64 Clang not installed." }
$Out = Join-Path $Root "build-aarch64-objects"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Include = Join-Path $Root "kernel\include"
$IncludeMre = Join-Path $Root "kernel\include\mre"
$Sources = @(
 "kernel/arch/aarch64/boot/entry.S",
 "kernel/arch/aarch64/boot/vectors.S",
 "kernel/arch/aarch64/mmu.c",
 "kernel/arch/aarch64/timer.c",
 "kernel/core/string.c",
 "kernel/core/fdt.c",
 "kernel/core/mmu.c",
 "kernel/core/timer_math.c",
 "kernel/core/uart_geni.c",
 "kernel/core/gic.c",
 "kernel/core/exception.c",
 "kernel/core/violet.c"
)
foreach ($Source in $Sources) {
    $InputPath = Join-Path $Root $Source
    $ObjectName = ($Source -replace '[\\/]', '_') + ".o"
    $ObjectPath = Join-Path $Out $ObjectName
    $Args = @(
      "--target=aarch64-none-elf", "-march=armv8-a",
      "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
      "-fno-pic", "-mgeneral-regs-only", "-Wall", "-Wextra", "-Werror",
      "-I$Include", "-I$IncludeMre", "-c", $InputPath, "-o", $ObjectPath
    )
    Write-Host "AArch64 compile: $Source"
    & $Compiler @Args
    if ($LASTEXITCODE -ne 0) { throw "AArch64 compile failed: $Source ($LASTEXITCODE)" }
}
Write-Host "AArch64 object compilation PASS: $($Sources.Count) source files"
