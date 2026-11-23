cmake_minimum_required(VERSION 3.20)
project(MRE_OS VERSION 0.2.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(MRE_BUILD_HOST_TESTS "Build host-side kernel and driver tests" ON)
option(MRE_ENABLE_SM6150 "Enable SM6150 platform modules when implemented" OFF)
option(MRE_BUILD_AARCH64_KERNEL "Build the freestanding AArch64 kernel image" OFF)
set(MRE_KERNEL_LINK_ADDRESS "" CACHE STRING "Link-time offset for the ARM64 Image; not a verified physical RAM base")

add_library(mre_driver_core STATIC
    drivers/core/driver_manager/driver_manager.c
    drivers/core/device_manager/device_manager.c
    drivers/core/firmware_manager/firmware_manager.c
)
target_include_directories(mre_driver_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel/include
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel/include/mre
    ${CMAKE_CURRENT_SOURCE_DIR}/hal/include
)

add_library(mre_kernel_core STATIC
    kernel/core/string.c
    kernel/core/fdt.c
    kernel/core/mmu.c
    kernel/core/timer_math.c
    kernel/core/uart_geni.c
    kernel/core/gic.c
    kernel/core/exception.c
    kernel/core/violet.c
    kernel/arch/aarch64/mmu.c
    kernel/arch/aarch64/timer.c
)
target_include_directories(mre_kernel_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel/include
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel/include/mre
    ${CMAKE_CURRENT_SOURCE_DIR}/hal/include
)

add_library(mre_ui_core STATIC ui/core/mre_ui.c)
target_include_directories(mre_ui_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/include
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel/include
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel/include/mre
)

foreach(target mre_driver_core mre_kernel_core mre_ui_core)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror -fno-builtin)
    endif()
endforeach()

if(MRE_BUILD_HOST_TESTS)
    enable_testing()
    add_executable(mre_core_tests tests/host/test_driver_manager.c)
    target_link_libraries(mre_core_tests PRIVATE mre_driver_core)
    add_test(NAME mre_core_tests COMMAND mre_core_tests)

    add_executable(mre_kernel_core_tests tests/host/test_kernel_core.c)
    target_link_libraries(mre_kernel_core_tests PRIVATE mre_kernel_core)
    add_test(NAME mre_kernel_core_tests COMMAND mre_kernel_core_tests)

    add_executable(mre_ui_tests tests/host/test_ui.c)
    target_link_libraries(mre_ui_tests PRIVATE mre_ui_core)
    add_test(NAME mre_ui_tests COMMAND mre_ui_tests)

    set(MRE_VIOLET_REFERENCE_DTB "${CMAKE_CURRENT_SOURCE_DIR}/build/analysis/violet-image-reference.dtb")
    if(EXISTS "${MRE_VIOLET_REFERENCE_DTB}")
        add_executable(mre_violet_reference_tests tests/host/test_violet_reference.c)
        target_link_libraries(mre_violet_reference_tests PRIVATE mre_kernel_core)
        add_test(NAME mre_violet_reference_tests
            COMMAND mre_violet_reference_tests "${MRE_VIOLET_REFERENCE_DTB}")
    endif()
endif()

if(MRE_BUILD_AARCH64_KERNEL)
    if(NOT CMAKE_C_COMPILER_TARGET MATCHES "aarch64" AND
       NOT CMAKE_C_COMPILER MATCHES "aarch64")
        message(FATAL_ERROR "AArch64 cross compiler required. Configure with clang --target=aarch64-none-elf or aarch64-none-elf-gcc.")
    endif()
    if(MRE_KERNEL_LINK_ADDRESS STREQUAL "")
        message(FATAL_ERROR "MRE_KERNEL_LINK_ADDRESS must be explicitly supplied from the ARM64 Image layout; this is not a verified physical RAM base.")
    endif()
    enable_language(ASM)
    add_executable(mre_kernel
        kernel/arch/aarch64/boot/entry.S
        kernel/arch/aarch64/boot/vectors.S
        kernel/arch/aarch64/mmu.c
        kernel/arch/aarch64/timer.c
        kernel/core/string.c
        kernel/core/fdt.c
        kernel/core/mmu.c
        kernel/core/timer_math.c
        kernel/core/uart_geni.c
        kernel/core/gic.c
        kernel/core/exception.c
        kernel/core/violet.c
    )
    target_include_directories(mre_kernel PRIVATE kernel/include kernel/include/mre)
    target_compile_options(mre_kernel PRIVATE -ffreestanding -fno-builtin -fno-stack-protector
        -fno-pic -mgeneral-regs-only -Wall -Wextra -Werror)
    target_link_options(mre_kernel PRIVATE -nostdlib -fuse-ld=lld -Wl,--build-id=none
        -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/kernel/arch/aarch64/linker.ld
        -Wl,--defsym=MRE_KERNEL_LINK_ADDRESS=${MRE_KERNEL_LINK_ADDRESS})
    set_target_properties(mre_kernel PROPERTIES SUFFIX ".elf")
endif()
