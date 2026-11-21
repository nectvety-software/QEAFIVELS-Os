# VQEAF OS x86_64 UEFI CMake Toolchain
#
# This toolchain configures CMake to cross-compile freestanding x86_64 UEFI
# binaries using the MinGW-w64 x86_64-w64-mingw32-gcc cross-compiler.
#
# Usage:
#   cmake -S . -B build -G "MinGW Makefiles" \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/VQEAFToolchain.cmake \
#     -DCMAKE_SYSTEM_NAME=Generic \
#     -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Cross-compiler: expected to be passed via -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc
# (build.py sets this automatically from VQEAF_CC environment variable).

# Bypass CMake compiler detection (UEFI flags break CMake's test compile).
# The compiler is a GNU MinGW-w64 cross-compiler; we already know it works.
set(CMAKE_C_COMPILER_ID "GNU" CACHE STRING "" FORCE)
set(CMAKE_C_COMPILER_VERSION "16.1.0" CACHE STRING "" FORCE)
set(CMAKE_C_COMPILER_LOADED TRUE CACHE BOOL "" FORCE)
set(CMAKE_C_COMPILER_WORKS TRUE CACHE BOOL "" FORCE)
set(CMAKE_C_COMPILER_ABI "elf" CACHE STRING "" FORCE)
set(CMAKE_C_COMPILER_ABI_LOADED TRUE CACHE BOOL "" FORCE)
set(CMAKE_C_STANDARD_COMPUTED_DEFAULT 11 CACHE STRING "" FORCE)
set(CMAKE_C_EXTENSIONS_COMPUTED_DEFAULT OFF CACHE BOOL "" FORCE)

# UEFI/freestanding compilation flags
set(CMAKE_C_FLAGS_INIT "-ffreestanding -fno-stack-protector -fno-stack-check -fshort-wchar -mno-red-zone -fno-builtin -Wall -Wextra -mgeneral-regs-only -mno-stack-arg-probe -std=c11")

set(CMAKE_C_FLAGS_DEBUG_INIT "-O0 -g")
set(CMAKE_C_FLAGS_RELEASE_INIT "-O2 -DNDEBUG")

# Linker flags for UEFI PE32+ executables
set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib -Wl,--subsystem,10 -Wl,--entry,efi_main -Wl,--disable-dynamicbase -Wl,--disable-nxcompat")

# Output suffix for UEFI binaries
set(CMAKE_C_OUTPUT_EXTENSION ".EFI")

# Do not search system default paths (we are freestanding)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

message(STATUS "VQEAF Toolchain: using compiler ${CMAKE_C_COMPILER}")
message(STATUS "VQEAF Toolchain: target x86_64 UEFI (freestanding)")
