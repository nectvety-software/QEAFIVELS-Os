/* SPDX-License-Identifier: MIT */
/* framework/simulator/qeaf_device.h - device profiles for the QEAPP VM. */
#ifndef QEAF_DEVICE_H
#define QEAF_DEVICE_H

#include <stddef.h>
#include <stdint.h>

/* A profile describes the physical device the host VM is emulating. The canvas
 * is the QEAPP app coordinate space; the panel is the real device framebuffer.
 * Raw panel touch input is mapped into the canvas with platform/violet's
 * panel_scale maths, so the VM and the phone agree on pixel and touch coords.
 */
typedef struct {
    const char *id;             /* stable CLI id, e.g. "violet" */
    const char *name;           /* human readable device name */
    uint32_t panel_width;       /* physical panel, portrait */
    uint32_t panel_height;
    uint32_t canvas_width;      /* QEAPP app canvas */
    uint32_t canvas_height;
    uint32_t rotation;          /* 0, 90, 180, 270 clockwise */
    const char *pixel_format;   /* canvas/scanout format */
    size_t memory_budget;       /* 0 -> broker default */
    uint32_t call_budget_ms;    /* 0 -> broker default */
} qeaf_device_profile_t;

/* NULL when the id is not known. Aliases are accepted for convenience. */
const qeaf_device_profile_t *qeaf_device_find(const char *id);

/* Enumerate profiles for `--list-devices`. */
size_t qeaf_device_count(void);
const qeaf_device_profile_t *qeaf_device_at(size_t index);

#endif /* QEAF_DEVICE_H */
