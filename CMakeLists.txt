cmake_minimum_required(VERSION 3.24)
project(VQEAF_OS VERSION 0.2.0 LANGUAGES C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)
set(EFI_FLAGS -ffreestanding -fno-stack-protector -fno-stack-check -fshort-wchar -mno-red-zone -fno-builtin -Wall -Wextra)
function(add_efi target source)
  add_executable(${target} ${source} bootloader/core/efi_guids.c)
  target_include_directories(${target} PRIVATE bootloader/core kernel/include)
  target_compile_options(${target} PRIVATE ${EFI_FLAGS})
  target_link_options(${target} PRIVATE -nostdlib -Wl,--subsystem,10 -Wl,--entry,efi_main -Wl,--disable-dynamicbase -Wl,--disable-nxcompat -Wl,-Map,kernel.map)
  set_target_properties(${target} PROPERTIES SUFFIX ".EFI" PREFIX "")
endfunction()
add_efi(BOOTX64 bootloader/core/boot_manager.c)
target_sources(BOOTX64 PRIVATE bootloader/core/display_mode.c system/ui/gfx.c system/ui/screen_common.c)
target_include_directories(BOOTX64 PRIVATE system/ui system/core/include)
add_efi(KERNEL kernel/arch/x86_64/entry.c)
target_sources(KERNEL PRIVATE kernel/arch/x86_64/arch.c kernel/arch/x86_64/switch.S kernel/arch/x86_64/vm.c kernel/arch/x86_64/pci.c kernel/arch/x86_64/rtc.c kernel/drivers/virtio_blk.c kernel/drivers/virtio_net.c kernel/drivers/ahci.c kernel/core/memory.c kernel/core/scheduler.c kernel/core/kheap.c kernel/core/elf.c kernel/core/ipc.c kernel/core/vfs.c kernel/core/security.c kernel/core/freestanding.c system/core/input.c system/core/power_menu.c system/core/driver.c system/core/capability.c system/core/storage.c system/core/storage_layout.c system/core/rescue.c system/core/power.c system/core/block.c system/core/partition.c system/core/fs_fat16.c system/ui/gfx.c system/ui/screen_common.c system/ui/recovery_screen.c system/ui/phone_shell.c system/ui/ui.c system/ui/update_screen.c)
target_include_directories(KERNEL PRIVATE system/core system/core/include system/ui)
target_compile_options(KERNEL PRIVATE -mgeneral-regs-only -mno-stack-arg-probe)
add_efi(RECOVERY recovery/core/recovery_main.c)
target_sources(RECOVERY PRIVATE bootloader/core/display_mode.c system/ui/gfx.c system/ui/screen_common.c system/ui/recovery_screen.c system/ui/update_screen.c)
target_include_directories(RECOVERY PRIVATE system/ui system/core/include)
add_library(vqeaf_system_init STATIC system/init/init.c)
target_include_directories(vqeaf_system_init PUBLIC system/init)
add_library(vqeaf_framework_abi STATIC framework/core/abi.c)
target_include_directories(vqeaf_framework_abi PUBLIC framework/application_api)
add_executable(vqeaf_ui_font_smoke tests/ui_font_smoke.c system/ui/gfx.c)
target_include_directories(vqeaf_ui_font_smoke PRIVATE system/ui)
add_executable(vqeaf_update_screen_smoke tests/update_screen_smoke.c system/ui/gfx.c system/ui/screen_common.c system/ui/update_screen.c)
target_include_directories(vqeaf_update_screen_smoke PRIVATE system/ui)
add_executable(vqeaf_screens_smoke tests/ui_screens_smoke.c system/ui/gfx.c system/ui/screen_common.c system/ui/recovery_screen.c system/ui/update_screen.c)
target_include_directories(vqeaf_screens_smoke PRIVATE system/ui)
add_executable(vqeaf_phone_shell_smoke tests/phone_shell_smoke.c system/ui/phone_shell.c system/ui/gfx.c system/ui/screen_common.c)
target_include_directories(vqeaf_phone_shell_smoke PRIVATE system/ui)
add_executable(vqeaf_clipboard_smoke tests/vqeaf_clipboard.c system/core/clipboard.c)
target_include_directories(vqeaf_clipboard_smoke PRIVATE system/core/include)
add_executable(vqeaf_core_smoke tests/core_smoke.c)
target_link_libraries(vqeaf_core_smoke PRIVATE vqeaf_system_init vqeaf_framework_abi)
add_executable(vqeaf_kernel_core_smoke tests/kernel_core_smoke.c tests/host_kernel_stubs.c kernel/core/memory.c kernel/core/scheduler.c kernel/core/kheap.c kernel/core/ipc.c kernel/core/vfs.c kernel/core/security.c)
target_include_directories(vqeaf_kernel_core_smoke PRIVATE kernel/include)
add_executable(vqeaf_core_services_smoke tests/core_services_smoke.c system/core/driver.c system/core/capability.c system/core/storage.c system/core/power.c system/core/block.c system/core/fs_fat16.c kernel/core/vfs.c kernel/core/security.c)
target_include_directories(vqeaf_core_services_smoke PRIVATE kernel/include system/core system/core/include)
add_executable(vqeaf_system_v03_smoke tests/system_v03_smoke.c system/core/driver.c system/core/capability.c system/core/storage.c system/core/power.c system/core/block.c system/core/partition.c system/core/fs_fat16.c system/core/discovery.c system/core/text.c system/core/service_manager.c system/core/input.c system/core/power_menu.c kernel/core/vfs.c kernel/core/security.c)
target_include_directories(vqeaf_system_v03_smoke PRIVATE kernel/include system/core system/core/include)
add_executable(vqeaf_vfs_mount_smoke tests/vfs_mount_smoke.c system/core/storage.c system/core/block.c system/core/fs_fat16.c kernel/core/vfs.c kernel/core/security.c)
target_include_directories(vqeaf_vfs_mount_smoke PRIVATE kernel/include system/core system/core/include)
add_executable(vqeaf_settings_smoke tests/vqeaf_settings.c system/core/settings.c system/core/settings_fat16.c system/core/storage.c system/core/block.c system/core/fs_fat16.c kernel/core/vfs.c kernel/core/security.c)
target_include_directories(vqeaf_settings_smoke PRIVATE kernel/include system/core system/core/include)
add_executable(vqeaf_violet_tap_smoke tests/violet_tap_smoke.c)
target_include_directories(vqeaf_violet_tap_smoke PRIVATE platform/violet)
add_executable(vqeaf_violet_drivers_test tests/violet_drivers.c
  drives/violet/violet_drivers.c
  platform/violet/vio_caps.c
  platform/violet/vqeaf_camera.c
  system/core/capability.c)
target_include_directories(vqeaf_violet_drivers_test PRIVATE
  drives/violet platform/violet system/core/include)
target_link_libraries(vqeaf_violet_drivers_test PRIVATE Threads::Threads)

# MRE FDT/Violet discovery, MMU planning, GIC and Generic Timer are integrated
# behind an explicit Violet bootstrap gate; QEMU-virt remains a separate path.
set(MRE_AARCH64_ROOT integrations/MRE-Os)
add_executable(vqeaf_violet_boot_probe_test
  tests/vqeaf_violet_boot_probe_test.c
  kernel/platform/aarch64/violet_boot_probe.c
  kernel/platform/aarch64/violet_bootstrap.c
  kernel/platform/aarch64/violet_memory.c
  kernel/platform/aarch64/violet_irq_bridge.c
  kernel/core/memory.c
  ${MRE_AARCH64_ROOT}/kernel/core/fdt.c
  ${MRE_AARCH64_ROOT}/kernel/core/violet.c
  ${MRE_AARCH64_ROOT}/kernel/core/mmu.c
  ${MRE_AARCH64_ROOT}/kernel/core/gic.c
  ${MRE_AARCH64_ROOT}/kernel/core/timer_math.c
  ${MRE_AARCH64_ROOT}/kernel/arch/aarch64/mmu.c
  ${MRE_AARCH64_ROOT}/kernel/arch/aarch64/timer.c)
target_include_directories(vqeaf_violet_boot_probe_test PRIVATE
  kernel/include
  ${MRE_AARCH64_ROOT}/kernel/include/mre)
target_compile_definitions(vqeaf_violet_boot_probe_test PRIVATE
  VQEAF_REFERENCE_DTB_PATH="${CMAKE_CURRENT_SOURCE_DIR}/platform/violet/device-tree/build/violet-reference.dtb")
add_executable(vqeaf_qvf_encode_test tests/vqeaf_qvf_encode.c platform/violet/qvf_encode.c)
target_include_directories(vqeaf_qvf_encode_test PRIVATE platform/violet)
add_executable(vqeaf_volume_lock_test tests/vqeaf_volume_lock_test.c)
target_include_directories(vqeaf_volume_lock_test PRIVATE platform/violet)
target_link_libraries(vqeaf_volume_lock_test PRIVATE Threads::Threads)
add_executable(vqeaf_wcn3990_smoke tests/wcn3990_driver_smoke.c kernel/drivers/wcn3990_snoc.c system/core/driver.c system/core/capability.c)
target_include_directories(vqeaf_wcn3990_smoke PRIVATE kernel/include system/core)
add_executable(vqeaf_wcn3990_phase2_smoke tests/wcn3990_phase2_smoke.c kernel/drivers/wcn3990_phase2.c system/core/driver.c system/core/capability.c)
target_include_directories(vqeaf_wcn3990_phase2_smoke PRIVATE kernel/include system/core)
add_executable(vqeaf_wcn3990_ce_smoke tests/wcn3990_ce_smoke.c kernel/drivers/wifi/wcn3990/wcn3990_ce.c)
target_include_directories(vqeaf_wcn3990_ce_smoke PRIVATE kernel/include)
add_executable(vqeaf_wcn3990_bt_smoke tests/wcn3990_bt_smoke.c kernel/drivers/bluetooth/wcn3990_bt.c)
target_include_directories(vqeaf_wcn3990_bt_smoke PRIVATE kernel/include)
add_executable(vqeaf_slim_qcom_ngd_smoke tests/slim_qcom_ngd_smoke.c kernel/drivers/slimbus/slim_qcom_ngd.c)
target_include_directories(vqeaf_slim_qcom_ngd_smoke PRIVATE kernel/include)
add_executable(vqeaf_slim_ngd_bringup_smoke tests/slim_ngd_bringup_smoke.c
  platform/violet/slim_ngd_bringup.c
  kernel/drivers/slimbus/slim_qcom_ngd.c)
target_include_directories(vqeaf_slim_ngd_bringup_smoke PRIVATE
  kernel/include platform/violet)
add_executable(vqeaf_wcn3990_slim_smoke tests/wcn3990_slim_smoke.c
  kernel/drivers/bluetooth/wcn3990_slim.c
  kernel/drivers/bluetooth/wcn3990_bt.c
  kernel/drivers/slimbus/slim_qcom_ngd.c)
target_include_directories(vqeaf_wcn3990_slim_smoke PRIVATE kernel/include)
add_executable(vqeaf_driver_core_smoke tests/driver_core_smoke.c system/core/driver.c)
target_include_directories(vqeaf_driver_core_smoke PRIVATE system/core system/core/include)

# The drives/ tree: hardware contracts (SPMI arbiter, regulators, GENI I2C,
# DISP_CC, touch) plus the catalog that registers them in the QEAFIVELS driver
# core. mre_dsi_host_sm6150.c and mre_dsi_phy14_sm6150.c are deliberately not
# listed: their register maps (dsi.xml.h, dsi_phy_14nm.xml.h) are not part of
# this repository, and inventing those offsets would write real hardware.
set(VQEAF_DRIVES_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/drives)
set(VQEAF_MRE_HAL_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/integrations/MRE-Os/hal/include)
set(VQEAF_MRE_KERNEL_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/integrations/MRE-Os/kernel/include/mre)
add_library(vqeaf_drives STATIC
  ${VQEAF_DRIVES_ROOT}/core/drive_catalog.c
  ${VQEAF_DRIVES_ROOT}/core/device_manager/device_manager.c
  ${VQEAF_DRIVES_ROOT}/core/driver_manager/driver_manager.c
  ${VQEAF_DRIVES_ROOT}/core/firmware_manager/firmware_manager.c
  ${VQEAF_DRIVES_ROOT}/bus/i2c/mre_geni_i2c.c
  ${VQEAF_DRIVES_ROOT}/bus/spmi/mre_spmi.c
  ${VQEAF_DRIVES_ROOT}/bus/spmi/mre_pmic_arb.c
  ${VQEAF_DRIVES_ROOT}/power/pmic/mre_pmic_regulator.c
  ${VQEAF_DRIVES_ROOT}/input/touchscreen/mre_touch.c
  ${VQEAF_DRIVES_ROOT}/display/clock/mre_sm6150_dispcc.c
  ${VQEAF_DRIVES_ROOT}/display/dsi/mre_reg_io.c
  ${VQEAF_DRIVES_ROOT}/display/dsi/mre_dsi_packet.c
  ${VQEAF_DRIVES_ROOT}/display/dsi/mre_dsi_reg_backend.c
  ${VQEAF_DRIVES_ROOT}/display/dsi/mre_panel_nt36672a.c
  ${CMAKE_CURRENT_SOURCE_DIR}/integrations/MRE-Os/kernel/core/fdt.c
  system/core/driver.c)
target_include_directories(vqeaf_drives PUBLIC
  ${VQEAF_DRIVES_ROOT}/core
  ${VQEAF_DRIVES_ROOT}/bus/i2c
  ${VQEAF_DRIVES_ROOT}/bus/spmi
  ${VQEAF_DRIVES_ROOT}/power/pmic
  ${VQEAF_DRIVES_ROOT}/input/touchscreen
  ${VQEAF_DRIVES_ROOT}/display/clock
  ${VQEAF_DRIVES_ROOT}/display/dsi
  ${VQEAF_MRE_HAL_INCLUDE}
  ${VQEAF_MRE_KERNEL_INCLUDE}
  system/core
  system/core/include)
add_executable(vqeaf_mre_drives_smoke tests/mre_drives_smoke.c
  ${VQEAF_DRIVES_ROOT}/display/dsi/mre_panel_identity.c
  tests/host_mre_gic_stub.c)
target_link_libraries(vqeaf_mre_drives_smoke PRIVATE vqeaf_drives)
target_include_directories(vqeaf_mre_drives_smoke PRIVATE tests)
add_executable(vqeaf_drive_catalog_smoke tests/drive_catalog_smoke.c
  framework/core/qeaf_drives_api.c
  tests/host_mre_gic_stub.c)
target_link_libraries(vqeaf_drive_catalog_smoke PRIVATE vqeaf_drives)
target_include_directories(vqeaf_drive_catalog_smoke PRIVATE tests)
target_include_directories(vqeaf_drive_catalog_smoke PRIVATE
  framework/application_api)
add_executable(vqeaf_sm6150_panel_smoke tests/sm6150_panel_smoke.c
  kernel/drivers/display/sm6150_panel.c)
target_include_directories(vqeaf_sm6150_panel_smoke PRIVATE kernel/include)
add_executable(vqeaf_sm6150_dsi_smoke tests/sm6150_dsi_smoke.c
  kernel/drivers/display/sm6150_dsi.c
  kernel/drivers/display/sm6150_panel.c)
target_include_directories(vqeaf_sm6150_dsi_smoke PRIVATE kernel/include)
add_executable(vqeaf_sm6150_display_smoke tests/sm6150_display_smoke.c
  kernel/drivers/display/sm6150_display.c
  kernel/drivers/display/sm6150_panel.c
  system/core/capability.c)
target_include_directories(vqeaf_sm6150_display_smoke PRIVATE kernel/include
  system/core system/core/include)
add_executable(vqeaf_uart_backend_smoke tests/uart_backend_smoke.c
  kernel/arch/aarch64/uart.c
  ${MRE_AARCH64_ROOT}/kernel/core/uart_geni.c)
target_include_directories(vqeaf_uart_backend_smoke PRIVATE kernel/include
  ${MRE_AARCH64_ROOT}/kernel/include/mre)
add_executable(vqeaf_geni_uart_smoke tests/mre_uart_geni_smoke.c
  ${MRE_AARCH64_ROOT}/kernel/core/uart_geni.c)
target_include_directories(vqeaf_geni_uart_smoke PRIVATE
  ${MRE_AARCH64_ROOT}/kernel/include/mre)
# Host tests are built in Release (-DNDEBUG), which would compile away every
# assert (and any call placed inside one). Tests must fail loudly: re-enable.
set(VQEAF_TEST_TARGETS vqeaf_ui_font_smoke vqeaf_update_screen_smoke
    vqeaf_screens_smoke vqeaf_phone_shell_smoke vqeaf_clipboard_smoke vqeaf_core_smoke
    vqeaf_kernel_core_smoke
    vqeaf_core_services_smoke vqeaf_system_v03_smoke vqeaf_vfs_mount_smoke
    vqeaf_settings_smoke vqeaf_violet_tap_smoke vqeaf_violet_drivers_test
    vqeaf_violet_boot_probe_test vqeaf_volume_lock_test
    vqeaf_qvf_encode_test
    vqeaf_wcn3990_smoke vqeaf_wcn3990_phase2_smoke
    vqeaf_wcn3990_ce_smoke vqeaf_wcn3990_bt_smoke
    vqeaf_slim_qcom_ngd_smoke vqeaf_slim_ngd_bringup_smoke
    vqeaf_wcn3990_slim_smoke vqeaf_driver_core_smoke
    vqeaf_sm6150_panel_smoke vqeaf_sm6150_dsi_smoke
    vqeaf_sm6150_display_smoke vqeaf_uart_backend_smoke vqeaf_geni_uart_smoke
    vqeaf_mre_drives_smoke vqeaf_drive_catalog_smoke)
foreach(t ${VQEAF_TEST_TARGETS})
  target_compile_options(${t} PRIVATE -UNDEBUG)
endforeach()
enable_testing()
add_test(NAME vqeaf_ui_font_smoke COMMAND vqeaf_ui_font_smoke)
add_test(NAME vqeaf_update_screen_smoke COMMAND vqeaf_update_screen_smoke)
add_test(NAME vqeaf_screens_smoke COMMAND vqeaf_screens_smoke)
add_test(NAME vqeaf_phone_shell_smoke COMMAND vqeaf_phone_shell_smoke)
add_test(NAME vqeaf_clipboard_smoke COMMAND vqeaf_clipboard_smoke)
add_test(NAME vqeaf_core_smoke COMMAND vqeaf_core_smoke)
add_test(NAME vqeaf_kernel_core_smoke COMMAND vqeaf_kernel_core_smoke)
add_test(NAME vqeaf_core_services_smoke COMMAND vqeaf_core_services_smoke)
add_test(NAME vqeaf_system_v03_smoke COMMAND vqeaf_system_v03_smoke)
add_test(NAME vqeaf_vfs_mount_smoke COMMAND vqeaf_vfs_mount_smoke)
add_test(NAME vqeaf_settings_smoke COMMAND vqeaf_settings_smoke)
add_test(NAME vqeaf_violet_tap_smoke COMMAND vqeaf_violet_tap_smoke)
add_test(NAME vqeaf_violet_drivers_test COMMAND vqeaf_violet_drivers_test)
add_test(NAME vqeaf_violet_boot_probe_test COMMAND vqeaf_violet_boot_probe_test)
add_test(NAME vqeaf_violet_abl_dtb_probe_test
  COMMAND vqeaf_violet_boot_probe_test
  ${CMAKE_CURRENT_SOURCE_DIR}/build/violet/violet-sm6150-abl.dtb)
add_test(NAME vqeaf_volume_lock_test COMMAND vqeaf_volume_lock_test)
add_test(NAME vqeaf_qvf_encode_test COMMAND vqeaf_qvf_encode_test)
add_test(NAME vqeaf_wcn3990_smoke COMMAND vqeaf_wcn3990_smoke)
add_test(NAME vqeaf_wcn3990_phase2_smoke COMMAND vqeaf_wcn3990_phase2_smoke)
add_test(NAME vqeaf_wcn3990_ce_smoke COMMAND vqeaf_wcn3990_ce_smoke)
add_test(NAME vqeaf_wcn3990_bt_smoke COMMAND vqeaf_wcn3990_bt_smoke)
add_test(NAME vqeaf_slim_qcom_ngd_smoke COMMAND vqeaf_slim_qcom_ngd_smoke)
add_test(NAME vqeaf_slim_ngd_bringup_smoke COMMAND vqeaf_slim_ngd_bringup_smoke)
add_test(NAME vqeaf_wcn3990_slim_smoke COMMAND vqeaf_wcn3990_slim_smoke)
add_test(NAME vqeaf_driver_core_smoke COMMAND vqeaf_driver_core_smoke)
add_test(NAME vqeaf_sm6150_panel_smoke COMMAND vqeaf_sm6150_panel_smoke)
add_test(NAME vqeaf_sm6150_dsi_smoke COMMAND vqeaf_sm6150_dsi_smoke)
add_test(NAME vqeaf_sm6150_display_smoke COMMAND vqeaf_sm6150_display_smoke)
add_test(NAME vqeaf_uart_backend_smoke COMMAND vqeaf_uart_backend_smoke)
add_test(NAME vqeaf_geni_uart_smoke COMMAND vqeaf_geni_uart_smoke)
add_test(NAME vqeaf_mre_drives_smoke COMMAND vqeaf_mre_drives_smoke
  ${CMAKE_CURRENT_SOURCE_DIR}/build/violet/violet-sm6150-abl.dtb)
add_test(NAME vqeaf_drive_catalog_smoke COMMAND vqeaf_drive_catalog_smoke)
