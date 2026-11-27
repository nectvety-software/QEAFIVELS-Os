#!/usr/bin/env python3
"""Classify QEAF Violet kernel/initramfs/USB/display boot evidence."""
import argparse
import re
from pathlib import Path


def classify(text):
    findings = []
    cmdline = ""
    for line in text.splitlines():
        if "Kernel command line:" in line:
            cmdline = line.split("Kernel command line:", 1)[1].strip()
            break
        if line.startswith("CMDLINE:"):
            cmdline = line.split(":", 1)[1].strip()
            break

    pid1_qeaf = "pid1: /init entry reached" in text
    pid1_android = (
        "/system/bin/initsecond_stage" in text or
        "PID1_EXE=/system/bin/init" in text or
        "PID1_CMDLINE=init second_stage" in text
    )
    skip_initramfs = "skip_initramfs" in cmdline or "skip_initramfs" in text
    initrd_freed = "Freeing initrd memory" in text

    if skip_initramfs:
        findings.append((
            "INITRAMFS_SKIP_FLAG_PRESENT",
            "bootargs contain skip_initramfs; Linux 4.14 populate_rootfs skips external initrd"
        ))
        if initrd_freed:
            findings.append((
                "EXTERNAL_INITRD_LOADED_THEN_SKIPPED",
                "kernel reserved and later freed the external initrd without executing its /init"
            ))

    if pid1_qeaf:
        findings.append(("QEAF_PID1_REACHED", "QEAF /init emitted its PID 1 entry marker"))
    elif pid1_android:
        findings.append(("ANDROID_PID1_RUNNING", "PID 1 is Android initsecond_stage, not QEAF"))
    elif not skip_initramfs:
        if any(x in text.lower() for x in ("kernel panic", "unable to mount root", "no working init found", "vfs:")):
            findings.append(("KERNEL_OR_HANDOFF_FAILURE", "kernel panic/rootfs/init handoff evidence found"))
        else:
            findings.append(("KERNEL_INITRAMFS_HANDOFF_UNPROVEN", "no QEAF PID 1 marker or decisive failure marker"))

    if "console: stdin/stdout/stderr attached to /dev/console" in text:
        findings.append(("QEAF_CONSOLE_READY", "QEAF PID 1 attached standard descriptors to /dev/console"))
    elif "console: /dev/console open failed" in text:
        findings.append(("QEAF_CONSOLE_DEGRADED", "QEAF console open failed"))

    if "usb: no UDC registered" in text:
        findings.append(("USB_UDC_MISSING", "kernel did not register a USB Device Controller"))
    elif "usb: binding UDC" in text or "usb: LUN image ready but UDC bind failed" in text:
        findings.append(("QEAF_USB_GADGET_BIND_FAILED", "QEAF configfs gadget could not bind to UDC"))
    elif "usb: mass storage bound to" in text or "QEAFRESCUE mass-storage gadget bound" in text:
        findings.append(("QEAF_USB_RESCUE_BOUND", "QEAF mass-storage gadget bound"))
    elif "a600000.dwc3" in text and ("state= configured" in text or "UDC_STATE=configured" in text):
        findings.append(("KERNEL_DWC3_UDC_PRESENT", "DWC3 UDC is registered/configured; QEAF gadget binding still needs PID 1"))
    elif "a600000.dwc3" in text or "a600000.dwc3" in cmdline:
        findings.append(("KERNEL_DWC3_PROBED", "DWC3 controller probe is visible in kernel logs"))
    elif not pid1_qeaf:
        findings.append(("QEAF_USB_RESCUE_NOT_REACHED", "no QEAF mass-storage bind marker"))

    if "Successfully bind display panel 'dsi_nt36672a_shenchao_vid_display'" in text:
        findings.append(("DISPLAY_NT36672A_BOUND", "kernel selected and bound NT36672A Shenchao panel"))
    elif "Successfully bind display panel 'dsi_ft8719_tianma_vid_display'" in text:
        findings.append(("DISPLAY_FT8719_BOUND", "kernel selected and bound FT8719 Tianma panel"))
    if "Initialized msm_drm" in text:
        findings.append(("DISPLAY_DRM_INITIALIZED", "MSM DRM/KMS initialized"))
    if "DRM_CONNECTOR=connected" in text or "card0-DSI-1: connected" in text:
        findings.append(("DISPLAY_CONNECTOR_CONNECTED", "DSI connector reports connected"))
    if "display: DRM/KMS init failed" in text:
        findings.append(("QEAF_DISPLAY_KMS_FAILED", "QEAF PID 1 could not initialize DRM/KMS"))
    elif "display: DRM device node absent" in text:
        findings.append(("QEAF_DISPLAY_DRM_NODE_ABSENT", "QEAF did not find /dev/dri/card0"))

    stage = None
    for line in text.splitlines():
        m = re.search(r"stage=([A-Za-z0-9_-]+)", line)
        if m:
            stage = m.group(1)
    if stage:
        findings.append(("P77_LAST_STAGE", stage))

    return findings


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("log", type=Path, help="combined boot evidence text")
    args = ap.parse_args()
    findings = classify(args.log.read_text(encoding="utf-8", errors="replace"))
    print(f"LOG={args.log}")
    for code, detail in findings:
        print(f"{code}: {detail}")


if __name__ == "__main__":
    main()
