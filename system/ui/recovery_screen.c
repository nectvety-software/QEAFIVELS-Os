#include "include/vqeaf/recovery_screen.h"
#include "include/vqeaf/screen_common.h"

static const char *const recovery_labels[VQEAF_RECOVERY_ITEM_COUNT] = {
    "Reboot System", "Apply Update", "Wipe Data", "Wipe Cache",
    "Advanced", "Power Off"
};
static const char *const recovery_notes[VQEAF_RECOVERY_ITEM_COUNT] = {
    "", "", "T\u1EAET", "T\u1EAET", "T\u1EAET", ""
};
static const VqeafIcon recovery_icons[VQEAF_RECOVERY_ITEM_COUNT] = {
    VQEAF_ICON_REBOOT, VQEAF_ICON_DOWNLOAD, VQEAF_ICON_TRASH,
    VQEAF_ICON_TRASH, VQEAF_ICON_GEAR, VQEAF_ICON_POWER
};
static const char *const fastboot_labels[VQEAF_FASTBOOT_ITEM_COUNT] = {
    "Continue", "Recovery Mode", "Update Mode", "Factory Reset", "Power Off"
};
static const VqeafIcon fastboot_icons[VQEAF_FASTBOOT_ITEM_COUNT] = {
    VQEAF_ICON_PLAY, VQEAF_ICON_REBOOT, VQEAF_ICON_DOWNLOAD,
    VQEAF_ICON_GEAR, VQEAF_ICON_POWER
};

const char *vqeaf_recovery_item_label(uint32_t index) {
    return index < VQEAF_RECOVERY_ITEM_COUNT ? recovery_labels[index] : 0;
}
const char *vqeaf_recovery_item_note(uint32_t index) {
    return index < VQEAF_RECOVERY_ITEM_COUNT ? recovery_notes[index] : 0;
}
int vqeaf_recovery_item_available(uint32_t index) {
    if (index >= VQEAF_RECOVERY_ITEM_COUNT) return 0;
    return index == VQEAF_RECOVERY_REBOOT_SYSTEM || index == VQEAF_RECOVERY_APPLY_UPDATE ||
           index == VQEAF_RECOVERY_POWER_OFF;
}
const char *vqeaf_fastboot_item_label(uint32_t index) {
    return index < VQEAF_FASTBOOT_ITEM_COUNT ? fastboot_labels[index] : 0;
}
int vqeaf_fastboot_item_available(uint32_t index) {
    if (index >= VQEAF_FASTBOOT_ITEM_COUNT) return 0;
    return index == VQEAF_FASTBOOT_CONTINUE || index == VQEAF_FASTBOOT_UPDATE ||
           index == VQEAF_FASTBOOT_POWER_OFF;
}

static uint32_t status_bar(const VqeafFramebuffer *fb, const VqeafScreenFrame *f,
                           const VqeafPalette *p, const char *text, uint32_t color,
                           const char *version) {
    uint32_t h = 40u * f->scale;
    uint32_t y = fb->height - h;
    vqeaf_gfx_rect(fb, f->x, y, f->w, h, p->bg);
    vqeaf_gfx_rect(fb, f->x, y, f->w, 1u, p->accent_dim);
    if (text && *text)
        vqeaf_gfx_text_centered(fb, y + 4u * f->scale, text, 1u, color);
    if (version && *version)
        vqeaf_gfx_text_centered(fb, y + 24u * f->scale, version, 1u, p->dim);
    return h;
}

void vqeaf_recovery_screen_render(const VqeafFramebuffer *fb, uint32_t selected,
                                  const char *status) {
    if (!fb || !fb->pixels || !fb->width || !fb->height) return;
    VqeafScreenFrame f = vqeaf_screen_frame(fb);
    VqeafPalette p = vqeaf_screen_palette(fb);
    uint32_t s = f.scale;
    vqeaf_gfx_clear(fb, p.bg);
    vqeaf_screen_waves(fb, fb->height - 40u * s, &p);
    vqeaf_screen_header(fb, &f, &p, "Recovery Mode");

    uint32_t hdr = 28u * s;
    uint32_t r = 26u * s;
    uint32_t step = r / 5u + 4u;
    uint32_t outer = r + 2u * step;
    uint32_t cx = f.x + f.w / 2u;
    uint32_t cy = hdr + 14u * s + outer;
    vqeaf_screen_badge(fb, cx, cy, r, 3, &p);

    uint32_t title_y = cy + outer + 12u * s;
    uint32_t title_scale = s + 2u;
    vqeaf_gfx_text_centered(fb, title_y, "VQEAF OS", title_scale, p.text);
    vqeaf_gfx_text_centered(fb, title_y + 16u * title_scale + 2u, "Recovery", s, p.accent);

    uint32_t menu_y = title_y + 16u * title_scale + 16u * s + 26u * s;
    uint32_t footer_h = 44u * s;
    uint32_t gap = 4u * s;
    uint32_t avail = fb->height > menu_y + footer_h ? fb->height - menu_y - footer_h : 0u;
    uint32_t row = vqeaf_screen_row_height(&f);
    uint32_t gaps = (VQEAF_RECOVERY_ITEM_COUNT - 1u) * gap;
    if ((uint64_t)row * VQEAF_RECOVERY_ITEM_COUNT + gaps > avail && avail > gaps)
        row = (avail - gaps) / VQEAF_RECOVERY_ITEM_COUNT;
    if (row < 18u) row = 18u;

    uint32_t y = menu_y;
    for (uint32_t i = 0; i < VQEAF_RECOVERY_ITEM_COUNT && y + row <= fb->height; i++) {
        vqeaf_screen_menu_row_h(fb, &f, &p, y, row, recovery_icons[i],
                                recovery_labels[i], recovery_notes[i],
                                i == selected, vqeaf_recovery_item_available(i));
        y += row + gap;
    }

    char line[80];
    uint32_t n = 0;
    const char *tip = "D\u00F9ng ph\u00EDm m\u0169i t\u00EAn | Enter \u0111\u1EC3 ch\u1ECDn";
    if (status && *status) {
        while (status[n] && n + 1u < sizeof line) { line[n] = status[n]; n++; }
        line[n] = 0;
    } else {
        while (tip[n] && n + 1u < sizeof line) { line[n] = tip[n]; n++; }
        line[n] = 0;
    }
    status_bar(fb, &f, &p, line, status && *status ? p.dim : p.accent,
               "VQEAF OS  |  Recovery v0.3");
}

static void info_row(const VqeafFramebuffer *fb, const VqeafScreenFrame *f,
                     const VqeafPalette *p, uint32_t x, uint32_t y,
                     const char *label, const char *value) {
    /* Metadata stays at 1x: at 2x the value column is narrower than the
     * reference strings and would paint over the card edge. */
    vqeaf_gfx_text(fb, x, y, label, 1u, p->dim);
    if (value) vqeaf_gfx_text(fb, x + 64u * f->scale, y, value, 1u, p->text);
}

void vqeaf_fastboot_screen_render(const VqeafFramebuffer *fb,
                                  const VqeafDeviceInfo *info, uint32_t selected,
                                  const char *status) {
    if (!fb || !fb->pixels || !fb->width || !fb->height) return;
    VqeafScreenFrame f = vqeaf_screen_frame(fb);
    VqeafPalette p = vqeaf_screen_palette(fb);
    uint32_t s = f.scale;
    vqeaf_gfx_clear(fb, p.bg);
    vqeaf_screen_waves(fb, fb->height - 40u * s, &p);
    vqeaf_screen_header(fb, &f, &p, "Fastboot Mode");

    uint32_t hdr = 28u * s;
    uint32_t cx = f.x + f.w / 2u;
    uint32_t badge_r = 30u * s;
    uint32_t badge_y = hdr + 14u * s + badge_r;
    vqeaf_screen_badge(fb, cx, badge_y, badge_r, 0, &p);
    uint32_t title_y = badge_y + badge_r + 12u * s;
    vqeaf_gfx_text_centered(fb, title_y, "VQEAF OS", s + 2u, p.text);
    vqeaf_gfx_text_centered(fb, title_y + 16u * (s + 2u) + 2u,
                            "Fastboot Mode", s, p.accent);

    uint32_t card_y = title_y + 16u * (s + 2u) + 20u * s;
    uint32_t card_h = info ? 96u * s : 0u;
    if (info) {
        vqeaf_gfx_rect(fb, f.x + 8u * s, card_y, f.w - 16u * s, card_h, p.panel);
        vqeaf_gfx_border(fb, f.x + 8u * s, card_y, f.w - 16u * s, card_h, 1u, p.accent_dim);
        uint32_t x = f.x + 16u * s, y = card_y + 8u * s;
        uint32_t dy = 16u * s;
        info_row(fb, &f, &p, x, y, "Device", info->device);
        info_row(fb, &f, &p, x, y + dy, "Model", info->model);
        info_row(fb, &f, &p, x, y + 2u * dy, "Version", info->version);
        info_row(fb, &f, &p, x, y + 3u * dy, "Build", info->build);
        info_row(fb, &f, &p, x, y + 4u * dy, "Mode", info->detail);
    }

    uint32_t menu_y = card_y + card_h + 12u * s;
    uint32_t footer_h = 44u * s;
    uint32_t gap = 3u * s;
    uint32_t avail = fb->height > menu_y + footer_h ? fb->height - menu_y - footer_h : 0u;
    uint32_t row = vqeaf_screen_row_height(&f);
    uint32_t gaps = (VQEAF_FASTBOOT_ITEM_COUNT - 1u) * gap;
    if ((uint64_t)row * VQEAF_FASTBOOT_ITEM_COUNT + gaps > avail && avail > gaps)
        row = (avail - gaps) / VQEAF_FASTBOOT_ITEM_COUNT;
    if (row < 18u) row = 18u;

    /* Fastboot labels already fill the row width; disabled entries are shown
     * by the dimmed label/icon and rejected on Enter, so no note is added. */
    uint32_t y = menu_y;
    for (uint32_t i = 0; i < VQEAF_FASTBOOT_ITEM_COUNT && y + row <= fb->height; i++) {
        vqeaf_screen_menu_row_h(fb, &f, &p, y, row, fastboot_icons[i],
                                fastboot_labels[i], "", i == selected,
                                vqeaf_fastboot_item_available(i));
        y += row + gap;
    }
    status_bar(fb, &f, &p, status && *status ? status : "",
               status && *status ? p.dim : p.accent_dim,
               info && info->footer ? info->footer
                                    : "VQEAF OS  |  x86_64 UEFI reference");
}
