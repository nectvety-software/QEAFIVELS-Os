#!/usr/bin/env bash
# VQEAF OS build environment — source this before building
export VQEAF_ROOT="D:/hdhQEAFIVELS"
export VQEAF_CC="C:/msys64/mingw64/bin/x86_64-w64-mingw32-gcc.EXE"
export VQEAF_CMAKE="C:/Program Files/CMake/bin/cmake.EXE"
export VQEAF_MAKE="C:/msys64/mingw64/bin/mingw32-make.EXE"
export VQEAF_QEMU="C:/Program Files/QEMU/qemu-system-x86_64.exe"
export VQEAF_OVMF_CODE="C:/Program Files/QEMU/share/edk2-x86_64-code.fd"
export VQEAF_OVMF_VARS="D:/hdhQEAFIVELS/build/edk2-x86_64-vars.fd"

# Add compiler to PATH if not already present
export PATH="C:/msys64/mingw64/bin:$PATH"
