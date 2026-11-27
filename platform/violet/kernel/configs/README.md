# Violet kernel config provenance

`qeaf-4.14.215-embedded.config` is extracted from the IKCONFIG blob inside the exact 4.14.215-azure-Pixel+ kernel payload used by QEAF and the stock-reference boot image.

SHA-256: `e457bd472e78090c0b447b30eff4a89069d5b1b5aa9b4b6b11a991b1d054e2c2`
Config header: Linux/arm64 4.14.122 Kernel Configuration

This is the actual embedded `.config`, not a reconstructed defconfig. It is also byte-identical to the config embedded in the TWRP 4.14.206-azure-perf+ kernel, even though the TWRP kernel binary is different.

Important options:
- `CONFIG_DRM_MSM=y`
- `CONFIG_DRM_MSM_DSI_STAGING=y`
- `CONFIG_DRM_MSM_DSI` disabled
- `CONFIG_USB_DWC3_DUAL_ROLE=y`
- `CONFIG_USB_GADGET=y`
- `CONFIG_USB_CONFIGFS_MASS_STORAGE=y`
- `CONFIG_BLK_DEV_INITRD=y`
- `CONFIG_INITRAMFS_FORCE` disabled
- `CONFIG_CMDLINE_EXTEND=y`

The config does not identify the exact source commit used for the 4.14.215 binary. Do not treat the 4.14.117 official board source or the 4.14.206 TWRP source as an exact source match.
