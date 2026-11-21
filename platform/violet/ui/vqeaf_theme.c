/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_theme.c - widget constructors for the phone shell. */
#include "vqeaf_theme.h"
#include "wallpaper.h"

#include "vqeaf/settings.h"

/* Zero-initialised, filled by vqeaf_theme_set() before anything is drawn. */
vqeaf_palette_t vqeaf_palette;
/* The installed shell starts in Light; Settings can still switch to Dark. */
static vqeaf_theme_mode_t g_theme_mode = VQEAF_THEME_LIGHT;
static int g_liquid_glass;

/* ---- dynamic colour (Material You style) --------------------------------- */
/* One seed colour generates every role, the way Material You derives the
 * palette from the wallpaper. The tones are a small integer HSL approximation
 * of Material's HCT: the neutrals keep the seed's hue at very low chroma, the
 * accent keeps it at high chroma. It is not a full HCT solver. */
static uint32_t g_seed;               /* 0 = the stock palette */
static int g_auto;                    /* 1 = seed follows the wallpaper */

static int clamp_i(int value, int lo, int hi)
{
    return value < lo ? lo : (value > hi ? hi : value);
}

/* Integer HSL: h 0..359, s/l 0..100. */
static lv_color_t hsl_color(int h, int s, int l)
{
    int sector, frac, c, x, m, r, g, b, light, chroma, span;

    h = ((h % 360) + 360) % 360;
    s = clamp_i(s, 0, 100);
    l = clamp_i(l, 0, 100);
    light = l * 10;                              /* 0..1000 */
    span = light * 2 > 1000 ? 2000 - light * 2 : light * 2;
    chroma = span * (s * 10) / 1000;             /* C = (1-|2L-1|) * S */
    sector = h / 60;
    frac = h % 60;
    x = (sector % 2 == 0) ? chroma * frac / 60
                          : chroma * (60 - frac) / 60;
    switch(sector) {
    case 0:  r = chroma; g = x;      b = 0;      break;
    case 1:  r = x;      g = chroma; b = 0;      break;
    case 2:  r = 0;      g = chroma; b = x;      break;
    case 3:  r = 0;      g = x;      b = chroma; break;
    case 4:  r = x;      g = 0;      b = chroma; break;
    default: r = chroma; g = 0;      b = x;      break;
    }
    m = light - chroma / 2;
    r = clamp_i((r + m) * 255 / 1000, 0, 255);
    g = clamp_i((g + m) * 255 / 1000, 0, 255);
    b = clamp_i((b + m) * 255 / 1000, 0, 255);
    return lv_color_make((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

static void rgb_to_hsl(uint32_t rgb, int *hue, int *sat, int *lum)
{
    int r = (int)((rgb >> 16) & 0xFFu);
    int g = (int)((rgb >> 8) & 0xFFu);
    int b = (int)(rgb & 0xFFu);
    int max = r > g ? (r > b ? r : b) : (g > b ? g : b);
    int min = r < g ? (r < b ? r : b) : (g < b ? g : b);
    int delta = max - min;
    int h = 0, s = 0;

    if(delta > 0) {
        int sum = max + min;
        s = sum > 255 ? delta * 100 / (510 - sum) : delta * 100 / sum;
        if(max == r) h = 60 * (g - b) / delta;
        else if(max == g) h = 60 * (b - r) / delta + 120;
        else h = 60 * (r - g) / delta + 240;
    }
    if(h < 0) h += 360;
    *hue = h;
    *sat = clamp_i(s, 0, 100);
    *lum = (max + min) * 100 / 510;
}

/* Material Design 3 roles. Derived from the shell palette so the components
 * re-tint with the seed instead of carrying their own hard-coded colours. */
static void palette_m3_roles(vqeaf_theme_mode_t mode, int hue)
{
    if(mode == VQEAF_THEME_LIGHT) {
        vqeaf_palette.primary = vqeaf_palette.accent;
        vqeaf_palette.on_primary = lv_color_hex(0xFFFFFF);
        vqeaf_palette.primary_container = hsl_color(hue, 55, 88);
        vqeaf_palette.on_primary_container = hsl_color(hue, 70, 18);
        vqeaf_palette.secondary_container = hsl_color(hue, 35, 90);
        vqeaf_palette.on_secondary_container = hsl_color(hue, 55, 16);
        vqeaf_palette.surface_container_low = vqeaf_palette.surface;
        vqeaf_palette.surface_container = vqeaf_palette.surface_2;
        vqeaf_palette.surface_container_high = vqeaf_palette.surface_3;
        vqeaf_palette.surface_container_highest = hsl_color(hue, 30, 82);
        vqeaf_palette.outline = vqeaf_palette.text_mute;
        vqeaf_palette.outline_variant = vqeaf_palette.line;
        vqeaf_palette.error = lv_color_hex(0xB3261E);
        vqeaf_palette.on_error = lv_color_hex(0xFFFFFF);
        vqeaf_palette.error_container = lv_color_hex(0xF9DEDC);
        vqeaf_palette.on_error_container = lv_color_hex(0x410E0B);
        vqeaf_palette.inverse_surface = vqeaf_palette.text;
        vqeaf_palette.inverse_on_surface = lv_color_hex(0xF2FBFF);
    }
    else {
        vqeaf_palette.primary = vqeaf_palette.accent;
        vqeaf_palette.on_primary = lv_color_hex(0x00201C);
        vqeaf_palette.primary_container = hsl_color(hue, 60, 22);
        vqeaf_palette.on_primary_container = hsl_color(hue, 80, 92);
        vqeaf_palette.secondary_container = hsl_color(hue, 30, 22);
        vqeaf_palette.on_secondary_container = hsl_color(hue, 25, 92);
        vqeaf_palette.surface_container_low = vqeaf_palette.surface;
        vqeaf_palette.surface_container = vqeaf_palette.surface_2;
        vqeaf_palette.surface_container_high = vqeaf_palette.surface_3;
        vqeaf_palette.surface_container_highest = vqeaf_palette.line;
        vqeaf_palette.outline = vqeaf_palette.text_mute;
        vqeaf_palette.outline_variant = vqeaf_palette.line;
        vqeaf_palette.error = lv_color_hex(0xFFB4AB);
        vqeaf_palette.on_error = lv_color_hex(0x690005);
        vqeaf_palette.error_container = lv_color_hex(0x93000A);
        vqeaf_palette.on_error_container = lv_color_hex(0xFFDAD6);
        vqeaf_palette.inverse_surface = lv_color_hex(0xEAF7F5);
        vqeaf_palette.inverse_on_surface = lv_color_hex(0x0C191B);
    }
}

static void palette_from_seed(vqeaf_theme_mode_t mode, uint32_t seed)
{
    int h, s, l, cap;

    rgb_to_hsl(seed, &h, &s, &l);
    /* Keep the seed's chroma, bounded so a grey seed still yields a usable
     * accent and a neon seed does not glow. */
    cap = clamp_i(s, 45, 82);

    if(mode == VQEAF_THEME_LIGHT) {
        vqeaf_palette.bg        = hsl_color(h, 42, 92);
        vqeaf_palette.bg_deep   = hsl_color(h, 52, 96);
        vqeaf_palette.surface   = hsl_color(h, 46, 99);
        vqeaf_palette.surface_2 = hsl_color(h, 40, 94);
        vqeaf_palette.surface_3 = hsl_color(h, 34, 89);
        vqeaf_palette.line      = hsl_color(h, 30, 78);
        vqeaf_palette.accent    = hsl_color(h, cap, 38);
        vqeaf_palette.accent_dk = hsl_color(h, cap, 29);
        vqeaf_palette.cyan      = hsl_color(h + 25, cap, 42);
        vqeaf_palette.green     = hsl_color(h + 90, cap, 34);
        vqeaf_palette.text      = hsl_color(h, 30, 12);
        vqeaf_palette.text_dim  = hsl_color(h, 20, 33);
        vqeaf_palette.text_mute = hsl_color(h, 14, 49);
    }
    else {
        vqeaf_palette.bg        = hsl_color(h, 12, 5);
        vqeaf_palette.bg_deep   = hsl_color(h, 14, 3);
        vqeaf_palette.surface   = hsl_color(h, 14, 10);
        vqeaf_palette.surface_2 = hsl_color(h, 16, 14);
        vqeaf_palette.surface_3 = hsl_color(h, 18, 19);
        vqeaf_palette.line      = hsl_color(h, 20, 25);
        vqeaf_palette.accent    = hsl_color(h, cap, 64);
        vqeaf_palette.accent_dk = hsl_color(h, cap, 46);
        vqeaf_palette.cyan      = hsl_color(h + 25, cap, 66);
        vqeaf_palette.green     = hsl_color(h + 90, cap, 62);
        vqeaf_palette.text      = hsl_color(h, 12, 95);
        vqeaf_palette.text_dim  = hsl_color(h, 12, 68);
        vqeaf_palette.text_mute = hsl_color(h, 10, 46);
    }

    /* Danger and warning stay semantic: a red delete button must read as
     * danger whatever the seed is. */
    if(mode == VQEAF_THEME_LIGHT) {
        vqeaf_palette.danger = lv_color_hex(0xC74646);
        vqeaf_palette.warn   = lv_color_hex(0xA87413);
    }
    else {
        vqeaf_palette.danger = lv_color_hex(0xFF6B6B);
        vqeaf_palette.warn   = lv_color_hex(0xFFB020);
    }
    palette_m3_roles(mode, h);
}

/* ---- wallpaper-derived dynamic colour (Material You) -------------------- */
/* vqeaf_wallpaper() paints the vector wallpaper from these fills. Sampling
 * them (area-weighted, biased toward chroma) makes Material You follow the
 * actual scene palette without decoding or loading a bitmap. */
typedef struct { uint32_t rgb; int weight; } wall_color_t;

static const wall_color_t wall_dark[] = {
    { 0x10232F, 34 },  /* deep blue sky */
    { 0x1C6669, 24 },  /* teal cloud */
    { 0x285878, 22 },  /* blue cloud */
    { 0x614B79, 20 },  /* violet sun glow */
};
static const wall_color_t wall_light[] = {
    { 0xE2DDEE, 40 },  /* pale lavender sky */
    { 0xFFF0E6, 14 },  /* warm horizon */
    { 0xB5A5C5, 12 }, { 0xD4AFC5, 10 },
    { 0xE9BCA8,  8 }, { 0xF5BFC0, 6 },
};

static uint32_t wallpaper_dominant(vqeaf_theme_mode_t mode)
{
    const wall_color_t *colors = mode == VQEAF_THEME_LIGHT ? wall_light :
                                                             wall_dark;
    int count = mode == VQEAF_THEME_LIGHT ?
                (int)(sizeof(wall_light) / sizeof(wall_light[0])) :
                (int)(sizeof(wall_dark) / sizeof(wall_dark[0]));
    uint64_t r = 0, g = 0, b = 0, weight_sum = 0;

    for(int i = 0; i < count; i++) {
        int h, s, l;
        uint64_t w;
        rgb_to_hsl(colors[i].rgb, &h, &s, &l);
        /* Area share, biased toward saturation so the accent region of the
         * wallpaper sets the hue instead of the large near-neutral base. */
        w = (uint64_t)colors[i].weight * (uint64_t)(20 + s);
        r += (uint64_t)((colors[i].rgb >> 16) & 0xFFu) * w;
        g += (uint64_t)((colors[i].rgb >> 8) & 0xFFu) * w;
        b += (uint64_t)(colors[i].rgb & 0xFFu) * w;
        weight_sum += w;
    }
    if(weight_sum == 0) return 0;
    return (uint32_t)((r / weight_sum) << 16) |
           (uint32_t)((g / weight_sum) << 8) |
           (uint32_t)(b / weight_sum);
}

void vqeaf_theme_set_seed(uint32_t rgb)
{
    g_auto = 0;
    g_seed = rgb & 0xFFFFFFu;
    vqeaf_theme_set(g_theme_mode);
}

uint32_t vqeaf_theme_seed(void)
{
    if(g_auto) return wallpaper_dominant(g_theme_mode);
    return g_seed;
}

void vqeaf_theme_set_auto(int on)
{
    g_auto = on ? 1 : 0;
    vqeaf_theme_set(g_theme_mode);
}

int vqeaf_theme_auto(void)
{
    return g_auto;
}

uint32_t vqeaf_theme_wallpaper_seed(void)
{
    return wallpaper_dominant(g_theme_mode);
}

void vqeaf_theme_set(vqeaf_theme_mode_t mode)
{
    g_theme_mode = mode == VQEAF_THEME_LIGHT ?
                   VQEAF_THEME_LIGHT : VQEAF_THEME_DARK;
    if(g_auto) {
        uint32_t derived = wallpaper_dominant(g_theme_mode);
        if(derived != 0) {
            palette_from_seed(g_theme_mode, derived);
            return;
        }
    }
    if(g_seed != 0) {
        palette_from_seed(g_theme_mode, g_seed);
        return;
    }
    if(g_theme_mode == VQEAF_THEME_LIGHT) {
        /* Sampled from the light-mode reference: pale sky wallpaper, white
         * frosted surfaces, dark ink text, deeper teal for contrast. */
        vqeaf_palette.bg        = lv_color_hex(0xF2F2F7);
        vqeaf_palette.bg_deep   = lv_color_hex(0xFFFFFF);
        vqeaf_palette.surface   = lv_color_hex(0xFFFFFF);
        vqeaf_palette.surface_2 = lv_color_hex(0xF2F2F7);
        vqeaf_palette.surface_3 = lv_color_hex(0xE5E5EA);
        vqeaf_palette.line      = lv_color_hex(0xD1D1D6);
        vqeaf_palette.accent    = lv_color_hex(0x007AFF);
        vqeaf_palette.accent_dk = lv_color_hex(0x005EC4);
        vqeaf_palette.cyan      = lv_color_hex(0x32ADE6);
        vqeaf_palette.green     = lv_color_hex(0x34C759);
        vqeaf_palette.text      = lv_color_hex(0x1C1C1E);
        vqeaf_palette.text_dim  = lv_color_hex(0x636366);
        vqeaf_palette.text_mute = lv_color_hex(0x8E8E93);
        vqeaf_palette.danger    = lv_color_hex(0xFF3B30);
        vqeaf_palette.warn      = lv_color_hex(0xFF9500);
    }
    else {
        vqeaf_palette.bg        = lv_color_hex(0x06100F);
        vqeaf_palette.bg_deep   = lv_color_hex(0x030809);
        vqeaf_palette.surface   = lv_color_hex(0x0C191B);
        vqeaf_palette.surface_2 = lv_color_hex(0x112629);
        vqeaf_palette.surface_3 = lv_color_hex(0x173236);
        vqeaf_palette.line      = lv_color_hex(0x1D3A3D);
        vqeaf_palette.accent    = lv_color_hex(0x1EE0C8);
        vqeaf_palette.accent_dk = lv_color_hex(0x0FA88F);
        vqeaf_palette.cyan      = lv_color_hex(0x38C8E8);
        vqeaf_palette.green     = lv_color_hex(0x34C97A);
        vqeaf_palette.text      = lv_color_hex(0xEAF7F5);
        vqeaf_palette.text_dim  = lv_color_hex(0x8FA7A6);
        vqeaf_palette.text_mute = lv_color_hex(0x5C7271);
        vqeaf_palette.danger    = lv_color_hex(0xFF6B6B);
        vqeaf_palette.warn      = lv_color_hex(0xFFB020);
    }
    {
        int h, s, l;
        rgb_to_hsl(lv_color_to_u32(vqeaf_palette.accent), &h, &s, &l);
        palette_m3_roles(g_theme_mode, h);
    }
}

vqeaf_theme_mode_t vqeaf_theme_get(void)
{
    return g_theme_mode;
}

void vqeaf_theme_set_liquid_glass(int on)
{
    g_liquid_glass = on ? 1 : 0;
}

int vqeaf_theme_liquid_glass(void)
{
    return g_liquid_glass;
}

/* LVGL has no backdrop blur in this renderer. Simulate the reference's glass
 * with a plum-tinted translucent gradient, a bright rim and a soft shadow. */
void vqeaf_glass_surface(lv_obj_t *obj, int32_t radius, lv_opa_t opacity)
{
    if(obj == NULL) return;
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x684080), 0);
    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(0xC77ADD), 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(obj, 0, 0);
    lv_obj_set_style_bg_grad_stop(obj, 255, 0);
    lv_obj_set_style_bg_opa(obj, opacity, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_border_width(obj, 2, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xF2D8FF), 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_80, 0);
    lv_obj_set_style_shadow_width(obj, 18, 0);
    lv_obj_set_style_shadow_ofs_y(obj, 7, 0);
    lv_obj_set_style_shadow_spread(obj, 0, 0);
    lv_obj_set_style_shadow_color(obj, lv_color_hex(0x351346), 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_60, 0);
}

/* --- typography weights --------------------------------------------------- */
/* The bold-text accessibility mode is owned by the Settings Service, not by
 * this file: the toggle writes through it and every role resolves from it, so
 * the preference has one source of truth. With no durable backend attached
 * (violet's read-only initramfs mounts no writable prefs partition) the service
 * keeps it in RAM for the session; attaching a prefs volume makes it persist. */
#define VQEAF_SETTING_BOLD_TEXT "display.bold_text"

void vqeaf_type_set_bold(int on)
{
    vqeaf_settings_set_bool(VQEAF_SETTING_BOLD_TEXT, on ? 1 : 0);
}

int vqeaf_type_get_bold(void)
{
    return vqeaf_settings_get_bool(VQEAF_SETTING_BOLD_TEXT, 0);
}

const lv_font_t *vqeaf_type_font_weight(vqeaf_font_role_t role,
                                       vqeaf_font_weight_t weight)
{
    if(weight == VQEAF_WEIGHT_BOLD) {
        switch(role) {
        case VQEAF_ROLE_CAPTION:  return &vqeaf_font_bold_20;
        case VQEAF_ROLE_BODY:     return &vqeaf_font_bold_24;
        case VQEAF_ROLE_TITLE:    return &vqeaf_font_bold_28;
        case VQEAF_ROLE_HEADLINE: return &vqeaf_font_bold_34;
        }
        return &vqeaf_font_bold_24;
    }
    switch(role) {
    case VQEAF_ROLE_CAPTION:  return &vqeaf_font_20;
    case VQEAF_ROLE_BODY:     return &vqeaf_font_24;
    case VQEAF_ROLE_TITLE:    return &vqeaf_font_28;
    case VQEAF_ROLE_HEADLINE: return &vqeaf_font_34;
    }
    return &vqeaf_font_24;
}

const lv_font_t *vqeaf_type_font(vqeaf_font_role_t role)
{
    return vqeaf_type_font_weight(role, vqeaf_type_get_bold() ? VQEAF_WEIGHT_BOLD :
                                                      VQEAF_WEIGHT_REGULAR);
}

lv_obj_t *vqeaf_obj(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    /* lv_obj_create() is clickable by default; a plain layout container must
     * not swallow clicks meant for its clickable parent (setting rows, dock
     * items, theme cards). Controls add CLICKABLE explicitly. */
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    /* Shell-level swipes (unlock, quick settings, page change) are handled on
     * the screen, so every chrome container lets the gesture reach it. */
    lv_obj_add_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return obj;
}

lv_obj_t *vqeaf_card(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, VQEAF_C_SURFACE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, VQEAF_C_LINE, 0);
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return card;
}

lv_obj_t *vqeaf_column(lv_obj_t *parent)
{
    lv_obj_t *box = vqeaf_obj(parent);
    lv_obj_set_width(box, lv_pct(100));
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 16, 0);
    return box;
}

lv_obj_t *vqeaf_row(lv_obj_t *parent)
{
    lv_obj_t *box = vqeaf_obj(parent);
    lv_obj_set_width(box, lv_pct(100));
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return box;
}

lv_obj_t *vqeaf_weighted_row(lv_obj_t *parent)
{
    lv_obj_t *box = vqeaf_obj(parent);
    lv_obj_set_width(box, lv_pct(100));
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
    /* START, not SPACE_BETWEEN: free space must go to flex grow so children
     * split it in ratio to their weight instead of being pushed apart. */
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(box, 16, 0);
    return box;
}

lv_obj_t *vqeaf_weighted_column(lv_obj_t *parent, int32_t height)
{
    lv_obj_t *box = vqeaf_obj(parent);
    lv_obj_set_width(box, lv_pct(100));
    lv_obj_set_height(box, height);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(box, 16, 0);
    return box;
}

void vqeaf_weight(lv_obj_t *obj, int weight)
{
    lv_obj_set_flex_grow(obj, weight > 0 ? weight : 0);
}

lv_obj_t *vqeaf_label(lv_obj_t *parent, const char *text,
                      const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    /* Readability on the photo wallpaper: a 1px contrasting outline keeps
     * dark text legible over the black-hole image (and light text over bright
     * areas) without changing the theme palette. */
    {
        lv_color32_t c = lv_color_to_32(color, LV_OPA_COVER);
        uint32_t lum = (c.red * 299u + c.green * 587u + c.blue * 114u) / 1000u;
        lv_obj_set_style_text_outline_stroke_width(label, 1, 0);
        lv_obj_set_style_text_outline_stroke_color(label,
            lum > 128u ? lv_color_black() : lv_color_white(), 0);
        lv_obj_set_style_text_outline_stroke_opa(label, 140, 0);
    }
    lv_obj_add_flag(label, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return label;
}

lv_obj_t *vqeaf_glyph(lv_obj_t *parent, const char *symbol,
                      const lv_font_t *font, lv_color_t color)
{
    return vqeaf_label(parent, symbol, font, color);
}

lv_obj_t *vqeaf_tile(lv_obj_t *parent, const char *symbol,
                     lv_color_t bg, lv_color_t fg,
                     int32_t size, int32_t radius)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_remove_style_all(tile);
    lv_obj_set_size(tile, size, size);
    lv_obj_set_style_bg_color(tile, bg, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tile, radius, 0);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *icon = vqeaf_glyph(tile, symbol, VQEAF_ICON, fg);
    lv_obj_center(icon);
    return tile;
}

void vqeaf_app_icon_style(lv_obj_t *tile, lv_color_t top,
                          lv_color_t bottom)
{
    if(tile == NULL) return;
    lv_obj_set_style_bg_color(tile, top, 0);
    lv_obj_set_style_bg_grad_color(tile, bottom, 0);
    lv_obj_set_style_bg_grad_dir(tile, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(tile, 0, 0);
    lv_obj_set_style_bg_grad_stop(tile, 255, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_border_color(tile, lv_color_white(), 0);
    lv_obj_set_style_border_opa(tile, (lv_opa_t)72, 0);
    lv_obj_set_style_shadow_width(tile, 10, 0);
    lv_obj_set_style_shadow_ofs_y(tile, 4, 0);
    lv_obj_set_style_shadow_spread(tile, 0, 0);
    lv_obj_set_style_shadow_color(tile, bottom, 0);
    lv_obj_set_style_shadow_opa(tile, (lv_opa_t)48, 0);
}

lv_obj_t *vqeaf_icon_img(lv_obj_t *parent, const lv_image_dsc_t *dsc,
                         int32_t size)
{
    if(dsc == NULL) return NULL;
    lv_obj_t *img = lv_image_create(parent);
    lv_obj_remove_style_all(img);
    lv_image_set_src(img, dsc);
    lv_obj_set_size(img, size, size);
    lv_image_set_scale(img, (uint32_t)size * 256 / dsc->header.w);
    lv_image_set_antialias(img, true);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(img, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return img;
}

void vqeaf_on_click(lv_obj_t *obj, lv_event_cb_t cb, void *user_data)
{
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, user_data);
}

/* --- Material Design 3 ---------------------------------------------------- */

const vqeaf_m3_elevation_t *vqeaf_m3_elevation_scale(void)
{
    /* Material 3 elevation tokens in dp; the shell has no dp unit, so the dp
     * value is the reference and the shadow table below scales it. */
    static const vqeaf_m3_elevation_t scale[6] = {
        { 0, 0 }, { 1, 1 }, { 2, 3 }, { 3, 6 }, { 4, 8 }, { 5, 12 },
    };
    return scale;
}

void vqeaf_m3_elevate(lv_obj_t *obj, int level)
{
    /* Shadow width/offset/opacity per elevation level, from the M3 scale. */
    static const int16_t width[6]  = { 0, 2, 4, 8, 12, 16 };
    static const int16_t offset[6] = { 0, 1, 2, 3, 4, 6 };
    static const uint8_t opa[6]    = { 0, 40, 60, 80, 100, 120 };

    if(obj == NULL) return;
    if(level < 0) level = 0;
    if(level > 5) level = 5;
    lv_obj_set_style_shadow_width(obj, width[level], 0);
    lv_obj_set_style_shadow_ofs_y(obj, offset[level], 0);
    lv_obj_set_style_shadow_spread(obj, 0, 0);
    lv_obj_set_style_shadow_color(obj, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(obj, (lv_opa_t)opa[level], 0);
}

void vqeaf_m3_state_layer_color(lv_obj_t *obj, lv_color_t tint)
{
    lv_opa_t base_opa;

    if(obj == NULL) return;
    base_opa = lv_obj_get_style_bg_opa(obj, 0);
    if(base_opa <= LV_OPA_TRANSP) {
        /* Transparent row/button: paint the tint itself. */
        lv_obj_set_style_bg_color(obj, tint, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(obj, VQEAF_M3_STATE_PRESSED,
                                LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(obj, tint, LV_STATE_FOCUSED);
        lv_obj_set_style_bg_opa(obj, VQEAF_M3_STATE_HOVER, LV_STATE_FOCUSED);
    }
    else {
        lv_color_t base = lv_obj_get_style_bg_color(obj, 0);
        lv_obj_set_style_bg_color(obj,
            lv_color_mix(tint, base, VQEAF_M3_STATE_PRESSED),
            LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(obj,
            lv_color_mix(tint, base, VQEAF_M3_STATE_HOVER),
            LV_STATE_FOCUSED);
    }
}

void vqeaf_m3_state_layer(lv_obj_t *obj)
{
    vqeaf_m3_state_layer_color(obj, VQEAF_C_TEXT);
}

static void m3_ripple_size_cb(void *var, int32_t value)
{
    lv_obj_set_size((lv_obj_t *)var, value, value);
}

static void m3_ripple_event_cb(lv_event_t *event)
{
    lv_obj_t *obj = lv_event_get_target(event);
    lv_obj_t *circle = (lv_obj_t *)lv_obj_get_user_data(obj);
    lv_event_code_t code = lv_event_get_code(event);
    int32_t span, diameter;

    if(circle == NULL) return;
    if(code == LV_EVENT_PRESSED) {
        lv_point_t point = { 0, 0 };
        lv_area_t area;
        lv_indev_t *indev = lv_indev_active();
        lv_anim_t anim;

        if(indev != NULL) lv_indev_get_point(indev, &point);
        lv_obj_get_coords(obj, &area);
        span = lv_obj_get_width(obj) > lv_obj_get_height(obj) ?
               lv_obj_get_width(obj) : lv_obj_get_height(obj);
        diameter = span * 2;
        if(diameter < 16) diameter = 16;
        lv_obj_align(circle, LV_ALIGN_CENTER,
                     point.x - (area.x1 + area.x2) / 2,
                     point.y - (area.y1 + area.y2) / 2);
        lv_obj_set_style_bg_opa(circle, VQEAF_M3_STATE_PRESSED, 0);
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, circle);
        lv_anim_set_values(&anim, 8, diameter);
        lv_anim_set_duration(&anim, VQEAF_M3_MOTION_MEDIUM);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&anim, m3_ripple_size_cb);
        lv_anim_start(&anim);
    }
    else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        lv_anim_delete(circle, m3_ripple_size_cb);
        lv_obj_set_style_bg_opa(circle, LV_OPA_TRANSP, 0);
        lv_obj_set_size(circle, 0, 0);
    }
}

void vqeaf_m3_ripple_color(lv_obj_t *obj, lv_color_t tint)
{
    lv_obj_t *circle;

    if(obj == NULL) return;
    vqeaf_m3_state_layer_color(obj, tint);
    /* Children are clipped to the object's rounded corners, so the ripple
     * follows a pill button or a card instead of spilling past its edge. */
    lv_obj_set_style_clip_corner(obj, true, 0);
    circle = lv_obj_create(obj);
    lv_obj_remove_style_all(circle);
    lv_obj_set_size(circle, 0, 0);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(circle, tint, 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(circle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(circle, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(circle, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(circle, LV_ALIGN_CENTER, 0, 0);
    /* Behind the label, like a Material state layer. */
    lv_obj_move_to_index(circle, 0);
    lv_obj_set_user_data(obj, circle);
    lv_obj_add_event_cb(obj, m3_ripple_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(obj, m3_ripple_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(obj, m3_ripple_event_cb, LV_EVENT_PRESS_LOST, NULL);
}

void vqeaf_m3_ripple(lv_obj_t *obj)
{
    vqeaf_m3_ripple_color(obj, VQEAF_C_TEXT);
}

lv_obj_t *vqeaf_m3_button(lv_obj_t *parent, const char *text,
                          vqeaf_m3_button_style_t style)
{
    lv_obj_t *button = lv_obj_create(parent);
    lv_color_t fg = VQEAF_C_ON_PRIMARY;

    lv_obj_remove_style_all(button);
    lv_obj_set_height(button, 56);
    lv_obj_set_width(button, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(button, VQEAF_M3_SHAPE_FULL, 0);
    lv_obj_set_style_pad_hor(button, 24, 0);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);

    switch(style) {
    case VQEAF_M3_TONAL:
        lv_obj_set_style_bg_color(button, VQEAF_C_SECONDARY_CONTAINER, 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        fg = VQEAF_C_ON_SECONDARY_CONTAINER;
        break;
    case VQEAF_M3_ELEVATED:
        lv_obj_set_style_bg_color(button, VQEAF_C_SURFACE_CONTAINER_LOW, 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        vqeaf_m3_elevate(button, 1);
        fg = VQEAF_C_PRIMARY;
        break;
    case VQEAF_M3_OUTLINED:
        lv_obj_set_style_border_width(button, 1, 0);
        lv_obj_set_style_border_color(button, VQEAF_C_OUTLINE, 0);
        fg = VQEAF_C_PRIMARY;
        break;
    case VQEAF_M3_TEXT:
        lv_obj_set_style_pad_hor(button, 12, 0);
        fg = VQEAF_C_PRIMARY;
        break;
    case VQEAF_M3_FILLED:
    default:
        lv_obj_set_style_bg_color(button, VQEAF_C_PRIMARY, 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        break;
    }

    vqeaf_label(button, text, VQEAF_FONT_BODY, fg);
    /* Filled/tonal buttons ripple with their own foreground colour, the way
     * Material darkens a primary fill instead of washing it out with white. */
    vqeaf_m3_ripple_color(button, fg);
    return button;
}

lv_obj_t *vqeaf_m3_fab(lv_obj_t *parent, const char *symbol)
{
    lv_obj_t *fab = vqeaf_tile(parent, symbol, VQEAF_C_PRIMARY_CONTAINER,
                               VQEAF_C_ON_PRIMARY_CONTAINER, 64,
                               VQEAF_M3_SHAPE_L);
    lv_obj_add_flag(fab, LV_OBJ_FLAG_CLICKABLE);
    vqeaf_m3_elevate(fab, 3);
    vqeaf_m3_ripple_color(fab, VQEAF_C_ON_PRIMARY_CONTAINER);
    return fab;
}

lv_obj_t *vqeaf_m3_fab_extended(lv_obj_t *parent, const char *symbol,
                                const char *label)
{
    lv_obj_t *fab = lv_obj_create(parent);

    lv_obj_remove_style_all(fab);
    lv_obj_set_height(fab, 64);
    lv_obj_set_width(fab, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(fab, 22, 0);
    lv_obj_set_style_pad_column(fab, 12, 0);
    lv_obj_set_style_radius(fab, VQEAF_M3_SHAPE_L, 0);
    lv_obj_set_style_bg_color(fab, VQEAF_C_PRIMARY_CONTAINER, 0);
    lv_obj_set_style_bg_opa(fab, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(fab, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(fab, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(fab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(fab, LV_OBJ_FLAG_CLICKABLE);
    vqeaf_glyph(fab, symbol, VQEAF_ICON_SM, VQEAF_C_ON_PRIMARY_CONTAINER);
    vqeaf_label(fab, label, VQEAF_FONT_BODY, VQEAF_C_ON_PRIMARY_CONTAINER);
    vqeaf_m3_elevate(fab, 3);
    vqeaf_m3_ripple_color(fab, VQEAF_C_ON_PRIMARY_CONTAINER);
    return fab;
}

lv_obj_t *vqeaf_m3_chip(lv_obj_t *parent, const char *text, int selected)
{
    lv_obj_t *chip = lv_obj_create(parent);

    lv_obj_remove_style_all(chip);
    lv_obj_set_height(chip, 40);
    lv_obj_set_width(chip, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(chip, VQEAF_M3_SHAPE_S, 0);
    lv_obj_set_style_pad_hor(chip, 16, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_border_color(chip,
        selected ? VQEAF_C_SECONDARY_CONTAINER : VQEAF_C_OUTLINE_VARIANT, 0);
    lv_obj_set_style_bg_color(chip, VQEAF_C_SECONDARY_CONTAINER, 0);
    lv_obj_set_style_bg_opa(chip, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    vqeaf_label(chip, text, VQEAF_FONT_CAPTION,
                selected ? VQEAF_C_ON_SECONDARY_CONTAINER : VQEAF_C_TEXT);
    vqeaf_m3_ripple_color(chip,
        selected ? VQEAF_C_ON_SECONDARY_CONTAINER : VQEAF_C_TEXT);
    return chip;
}

lv_obj_t *vqeaf_m3_switch(lv_obj_t *parent, int on)
{
    lv_obj_t *track = lv_obj_create(parent);
    lv_obj_t *thumb;

    lv_obj_remove_style_all(track);
    lv_obj_set_size(track, 52, 32);
    lv_obj_set_style_radius(track, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(track,
        on ? VQEAF_C_PRIMARY : VQEAF_C_SURFACE_CONTAINER_HIGHEST, 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(track, on ? 0 : 2, 0);
    lv_obj_set_style_border_color(track, VQEAF_C_OUTLINE, 0);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_CLICKABLE);

    thumb = lv_obj_create(track);
    lv_obj_remove_style_all(thumb);
    /* Material 3: a 24 px thumb when on, a 16 px dot inside the outline when
     * off. The track stays 52x32 so the control keeps its touch target. */
    lv_obj_set_size(thumb, on ? 24 : 16, on ? 24 : 16);
    lv_obj_set_style_radius(thumb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(thumb,
        on ? VQEAF_C_ON_PRIMARY : VQEAF_C_OUTLINE, 0);
    lv_obj_set_style_bg_opa(thumb, LV_OPA_COVER, 0);
    lv_obj_remove_flag(thumb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(thumb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(thumb, on ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID,
                 on ? -4 : 4, 0);
    return track;
}

lv_obj_t *vqeaf_m3_card(lv_obj_t *parent, int32_t w, int32_t h,
                        vqeaf_m3_card_style_t style)
{
    lv_obj_t *card = vqeaf_card(parent, w, h);

    lv_obj_set_style_radius(card, VQEAF_M3_SHAPE_M, 0);
    switch(style) {
    case VQEAF_M3_CARD_ELEVATED:
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_bg_color(card, VQEAF_C_SURFACE_CONTAINER_LOW, 0);
        vqeaf_m3_elevate(card, 1);
        break;
    case VQEAF_M3_CARD_FILLED:
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_bg_color(card, VQEAF_C_SURFACE_CONTAINER_HIGH, 0);
        break;
    case VQEAF_M3_CARD_OUTLINED:
    default:
        lv_obj_set_style_border_color(card, VQEAF_C_OUTLINE_VARIANT, 0);
        break;
    }
    return card;
}

/* --- Material Design 3 phase 2 ------------------------------------------- */

const lv_font_t *vqeaf_m3_type(vqeaf_m3_type_t type)
{
    switch(type) {
    case VQEAF_M3_TYPE_DISPLAY_LARGE:  return &vqeaf_font_bold_96;
    case VQEAF_M3_TYPE_DISPLAY_SMALL:  return &vqeaf_font_bold_56;
    case VQEAF_M3_TYPE_HEADLINE_LARGE: return vqeaf_type_font(VQEAF_ROLE_HEADLINE);
    case VQEAF_M3_TYPE_HEADLINE_SMALL: return vqeaf_type_font(VQEAF_ROLE_TITLE);
    case VQEAF_M3_TYPE_TITLE_LARGE:
        return vqeaf_type_font_weight(VQEAF_ROLE_TITLE, VQEAF_WEIGHT_BOLD);
    case VQEAF_M3_TYPE_TITLE_MEDIUM:   return vqeaf_type_font(VQEAF_ROLE_TITLE);
    case VQEAF_M3_TYPE_TITLE_SMALL:
        return vqeaf_type_font_weight(VQEAF_ROLE_BODY, VQEAF_WEIGHT_BOLD);
    case VQEAF_M3_TYPE_BODY_LARGE:     return vqeaf_type_font(VQEAF_ROLE_BODY);
    case VQEAF_M3_TYPE_BODY_MEDIUM:    return vqeaf_type_font(VQEAF_ROLE_CAPTION);
    case VQEAF_M3_TYPE_BODY_SMALL:     return vqeaf_type_font(VQEAF_ROLE_CAPTION);
    case VQEAF_M3_TYPE_LABEL_LARGE:
        return vqeaf_type_font_weight(VQEAF_ROLE_CAPTION, VQEAF_WEIGHT_BOLD);
    case VQEAF_M3_TYPE_LABEL_MEDIUM:   return vqeaf_type_font(VQEAF_ROLE_CAPTION);
    case VQEAF_M3_TYPE_LABEL_SMALL:    return vqeaf_type_font(VQEAF_ROLE_CAPTION);
    }
    return vqeaf_type_font(VQEAF_ROLE_BODY);
}

const vqeaf_m3_type_token_t *vqeaf_m3_type_token(vqeaf_m3_type_t type)
{
    /* Nominal Material 3 metrics (dp size, tracking in 1/100 em, weight).
     * The resolved face is the nearest rasterised size (see vqeaf_m3_type). */
    static const vqeaf_m3_type_token_t tokens[] = {
        { 57, -25, 400 },  /* display large  */
        { 45,   0, 400 },  /* display small  */
        { 32,   0, 400 },  /* headline large */
        { 24,   0, 400 },  /* headline small */
        { 22,   0, 500 },  /* title large    */
        { 16,  15, 500 },  /* title medium   */
        { 14,  10, 500 },  /* title small    */
        { 16,  50, 400 },  /* body large     */
        { 14,  25, 400 },  /* body medium    */
        { 12,  40, 400 },  /* body small     */
        { 14,  10, 500 },  /* label large    */
        { 12,  50, 500 },  /* label medium   */
        { 11,  50, 500 }   /* label small    */
    };
    if((int)type < 0 || (int)type > VQEAF_M3_TYPE_LABEL_SMALL)
        return &tokens[VQEAF_M3_TYPE_BODY_LARGE];
    return &tokens[type];
}

static lv_obj_t *m3_icon_button(lv_obj_t *parent, const char *symbol,
                                lv_color_t fg, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 48, 48);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *g = vqeaf_glyph(btn, symbol, VQEAF_ICON_SM, fg);
    lv_obj_center(g);
    vqeaf_m3_state_layer(btn);
    if(cb) vqeaf_on_click(btn, cb, ud);
    return btn;
}

lv_obj_t *vqeaf_m3_top_app_bar(lv_obj_t *parent, const char *title,
                               const char *nav_symbol, const char *action_symbol,
                               lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_width(bar, lv_pct(100));
    lv_obj_set_height(bar, 64);
    lv_obj_set_style_bg_color(bar, VQEAF_C_SURFACE_CONTAINER, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(bar, 8, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    if(nav_symbol)
        m3_icon_button(bar, nav_symbol, VQEAF_C_TEXT, cb, user_data);
    lv_obj_t *t = vqeaf_label(bar, title, vqeaf_m3_type(VQEAF_M3_TYPE_TITLE_LARGE),
                              VQEAF_C_TEXT);
    lv_obj_set_flex_grow(t, 1);
    lv_obj_set_style_pad_left(t, 8, 0);
    if(action_symbol)
        m3_icon_button(bar, action_symbol, VQEAF_C_TEXT, cb, user_data);
    return bar;
}

lv_obj_t *vqeaf_m3_navigation_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_width(bar, lv_pct(100));
    lv_obj_set_height(bar, 80);
    lv_obj_set_style_bg_color(bar, VQEAF_C_SURFACE_CONTAINER, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    return bar;
}

lv_obj_t *vqeaf_m3_nav_item(lv_obj_t *bar, const char *symbol, const char *label,
                            int selected)
{
    lv_obj_t *item = lv_obj_create(bar);
    lv_obj_remove_style_all(item);
    lv_obj_set_size(item, LV_SIZE_CONTENT, 80);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(item, 8, 0);
    lv_obj_set_style_pad_row(item, 4, 0);
    lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    /* Active indicator: a 64x32 secondary-container pill behind the icon. */
    lv_obj_t *pill = lv_obj_create(item);
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, 64, 32);
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pill, VQEAF_C_SECONDARY_CONTAINER, 0);
    lv_obj_set_style_bg_opa(pill, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *g = vqeaf_glyph(pill, symbol, VQEAF_ICON_SM,
        selected ? VQEAF_C_ON_SECONDARY_CONTAINER : VQEAF_C_TEXT_DIM);
    lv_obj_center(g);
    vqeaf_label(item, label, vqeaf_m3_type(VQEAF_M3_TYPE_LABEL_MEDIUM),
                selected ? VQEAF_C_TEXT : VQEAF_C_TEXT_DIM);
    vqeaf_m3_ripple(item);
    return item;
}

lv_obj_t *vqeaf_m3_list_item(lv_obj_t *parent, const char *leading_symbol,
                             const char *headline, const char *supporting,
                             lv_obj_t **trailing_out)
{
    lv_obj_t *item = lv_obj_create(parent);
    lv_obj_remove_style_all(item);
    lv_obj_set_width(item, lv_pct(100));
    lv_obj_set_height(item, supporting ? 72 : 56);
    lv_obj_set_style_pad_hor(item, 16, 0);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(item, 16, 0);
    lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    if(leading_symbol)
        vqeaf_glyph(item, leading_symbol, VQEAF_ICON_SM, VQEAF_C_TEXT_DIM);
    lv_obj_t *col = vqeaf_column(item);
    lv_obj_set_flex_grow(col, 1);
    vqeaf_label(col, headline, vqeaf_m3_type(VQEAF_M3_TYPE_BODY_LARGE),
                VQEAF_C_TEXT);
    if(supporting)
        vqeaf_label(col, supporting, vqeaf_m3_type(VQEAF_M3_TYPE_BODY_MEDIUM),
                    VQEAF_C_TEXT_DIM);
    if(trailing_out) {
        lv_obj_t *slot = vqeaf_obj(item);
        lv_obj_set_size(slot, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        *trailing_out = slot;
    }
    vqeaf_m3_ripple(item);
    return item;
}

lv_obj_t *vqeaf_m3_dialog(lv_obj_t *parent, const char *headline,
                          const char *supporting, const char *confirm_text,
                          const char *dismiss_text, lv_obj_t **confirm_out,
                          lv_obj_t **dismiss_out)
{
    lv_obj_t *scrim = lv_obj_create(parent);
    lv_obj_remove_style_all(scrim);
    lv_obj_set_pos(scrim, 0, 0);
    lv_obj_set_size(scrim, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(scrim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scrim, 102, 0);  /* 40% scrim */
    lv_obj_remove_flag(scrim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_CLICKABLE);  /* swallow background taps */
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_FLOATING);

    lv_obj_t *card = lv_obj_create(scrim);
    lv_obj_remove_style_all(card);
    lv_obj_set_width(card, lv_pct(86));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(card, VQEAF_M3_SHAPE_XL, 0);
    lv_obj_set_style_bg_color(card, VQEAF_C_SURFACE_CONTAINER_HIGH, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(card, 24, 0);
    lv_obj_set_style_pad_row(card, 16, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    vqeaf_m3_elevate(card, 3);

    lv_obj_t *title = vqeaf_label(card, headline,
                                 vqeaf_m3_type(VQEAF_M3_TYPE_HEADLINE_SMALL),
                                 VQEAF_C_TEXT);
    lv_obj_set_width(title, lv_pct(100));
    if(supporting) {
        /* Without an explicit width the label grows past the card and the
         * supporting text is cut at the screen edge. */
        lv_obj_t *body = vqeaf_label(card, supporting,
                                     vqeaf_m3_type(VQEAF_M3_TYPE_BODY_MEDIUM),
                                     VQEAF_C_TEXT_DIM);
        lv_obj_set_width(body, lv_pct(100));
    }
    lv_obj_t *actions = vqeaf_row(card);
    lv_obj_set_width(actions, lv_pct(100));
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(actions, 8, 0);
    lv_obj_t *dismiss = NULL, *confirm = NULL;
    if(dismiss_text) dismiss = vqeaf_m3_button(actions, dismiss_text, VQEAF_M3_TEXT);
    if(confirm_text) confirm = vqeaf_m3_button(actions, confirm_text, VQEAF_M3_TEXT);
    if(confirm_out) *confirm_out = confirm;
    if(dismiss_out) *dismiss_out = dismiss;
    return scrim;
}

lv_obj_t *vqeaf_m3_snackbar(lv_obj_t *parent, const char *message,
                            const char *action_text, lv_obj_t **action_out)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_width(bar, lv_pct(92));
    lv_obj_set_height(bar, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(bar, 48, 0);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_set_style_radius(bar, VQEAF_M3_SHAPE_XS, 0);
    lv_obj_set_style_bg_color(bar, VQEAF_C_INVERSE_SURFACE, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(bar, 16, 0);
    lv_obj_set_style_pad_ver(bar, 8, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 8, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_FLOATING);
    vqeaf_m3_elevate(bar, 3);
    lv_obj_t *msg = vqeaf_label(bar, message,
                                vqeaf_m3_type(VQEAF_M3_TYPE_BODY_MEDIUM),
                                VQEAF_C_INVERSE_ON_SURFACE);
    lv_obj_set_flex_grow(msg, 1);
    lv_obj_t *action = NULL;
    if(action_text) {
        action = vqeaf_m3_button(bar, action_text, VQEAF_M3_TEXT);
        /* On the inverse surface the action reads in the accent, not the
         * on-surface primary used by dialogs. Child 0 is the ripple circle,
         * child 1 is the label. */
        lv_obj_t *lbl = lv_obj_get_child(action, 1);
        if(lbl) lv_obj_set_style_text_color(lbl, VQEAF_C_PRIMARY, 0);
    }
    if(action_out) *action_out = action;
    return bar;
}

static int m3_clamp100(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }

static void m3_slider_event_cb(lv_event_t *event)
{
    lv_obj_t *s = lv_event_get_target_obj(event);
    lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev;
    lv_point_t point;
    lv_area_t area;
    int32_t w, x;

    if(code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) return;
    indev = lv_indev_active();
    if(indev == NULL) return;
    lv_indev_get_point(indev, &point);
    lv_obj_get_coords(s, &area);
    w = area.x2 - area.x1 + 1;
    if(w <= 0) return;
    x = point.x - area.x1;
    if(x < 0) x = 0;
    if(x > w) x = w;
    vqeaf_m3_slider_set(s, (int)(x * 100 / w));
    lv_obj_send_event(s, LV_EVENT_VALUE_CHANGED, NULL);
}

lv_obj_t *vqeaf_m3_slider(lv_obj_t *parent, int value)
{
    lv_obj_t *s = lv_obj_create(parent);
    lv_obj_remove_style_all(s);
    lv_obj_set_width(s, lv_pct(100));
    lv_obj_set_height(s, 48);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *track = lv_obj_create(s);
    lv_obj_remove_style_all(track);
    lv_obj_set_size(track, lv_pct(100), 8);
    lv_obj_align(track, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(track, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(track, VQEAF_C_SURFACE_CONTAINER_HIGHEST, 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *active = lv_obj_create(s);
    lv_obj_remove_style_all(active);
    lv_obj_set_height(active, 8);
    lv_obj_set_style_radius(active, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(active, VQEAF_C_PRIMARY, 0);
    lv_obj_set_style_bg_opa(active, LV_OPA_COVER, 0);
    lv_obj_remove_flag(active, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *thumb = lv_obj_create(s);
    lv_obj_remove_style_all(thumb);
    lv_obj_set_size(thumb, 24, 24);
    lv_obj_set_style_radius(thumb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(thumb, VQEAF_C_PRIMARY, 0);
    lv_obj_set_style_bg_opa(thumb, LV_OPA_COVER, 0);
    lv_obj_remove_flag(thumb, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    vqeaf_m3_elevate(thumb, 1);
    lv_obj_add_flag(s, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s, m3_slider_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s, m3_slider_event_cb, LV_EVENT_PRESSING, NULL);
    vqeaf_m3_slider_set(s, value);
    return s;
}

void vqeaf_m3_slider_set(lv_obj_t *s, int value)
{
    if(s == NULL) return;
    value = m3_clamp100(value);
    lv_obj_set_user_data(s, (void *)(uintptr_t)value);
    lv_obj_update_layout(s);
    int32_t w = lv_obj_get_width(s);
    if(w <= 0) w = VQEAF_SCREEN_W - 2 * VQEAF_PAD;
    lv_obj_t *active = lv_obj_get_child(s, 1);
    lv_obj_t *thumb = lv_obj_get_child(s, 2);
    int32_t aw = w * value / 100;
    lv_obj_set_width(active, aw);
    lv_obj_align(active, LV_ALIGN_LEFT_MID, 0, 0);
    int32_t tx = aw - 12;
    if(tx < 0) tx = 0;
    if(tx > w - 24) tx = w - 24;
    lv_obj_set_pos(thumb, tx, (48 - 24) / 2);
}

lv_obj_t *vqeaf_m3_progress_linear(lv_obj_t *parent, int value)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_width(bar, lv_pct(100));
    lv_obj_set_height(bar, 8);
    lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(bar, VQEAF_C_SURFACE_CONTAINER_HIGHEST, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *ind = lv_obj_create(bar);
    lv_obj_remove_style_all(ind);
    lv_obj_set_height(ind, 8);
    lv_obj_set_style_radius(ind, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ind, VQEAF_C_PRIMARY, 0);
    lv_obj_set_style_bg_opa(ind, LV_OPA_COVER, 0);
    lv_obj_remove_flag(ind, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    vqeaf_m3_progress_set(bar, value);
    return bar;
}

void vqeaf_m3_progress_set(lv_obj_t *bar, int value)
{
    if(bar == NULL) return;
    value = m3_clamp100(value);
    lv_obj_update_layout(bar);
    int32_t w = lv_obj_get_width(bar);
    if(w <= 0) w = VQEAF_SCREEN_W - 2 * VQEAF_PAD;
    lv_obj_t *ind = lv_obj_get_child(bar, 0);
    lv_obj_set_width(ind, w * value / 100);
    lv_obj_align(ind, LV_ALIGN_LEFT_MID, 0, 0);
}

int vqeaf_m3_slider_get(lv_obj_t *slider)
{
    if(slider == NULL) return 0;
    return (int)(uintptr_t)lv_obj_get_user_data(slider);
}

static void m3_progress_sweep_cb(void *var, int32_t value)
{
    lv_obj_set_x((lv_obj_t *)var, value);
}

lv_obj_t *vqeaf_m3_progress_indeterminate(lv_obj_t *parent)
{
    lv_obj_t *bar = vqeaf_m3_progress_linear(parent, 0);
    lv_obj_set_style_clip_corner(bar, true, 0);
    lv_obj_update_layout(bar);
    int32_t w = lv_obj_get_width(bar);
    if(w <= 0) w = VQEAF_SCREEN_W - 2 * VQEAF_PAD;
    lv_obj_t *ind = lv_obj_get_child(bar, 0);
    int32_t iw = w * 35 / 100;
    lv_obj_set_width(ind, iw);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, ind);
    lv_anim_set_exec_cb(&anim, m3_progress_sweep_cb);
    /* Scan fully inside the track (0 .. w-iw) and ping-pong, so the indicator
     * is visible at t=0 and never clips away to an empty-looking track. */
    lv_anim_set_values(&anim, 0, w - iw);
    lv_anim_set_duration(&anim, VQEAF_M3_MOTION_LONG * 2);
    lv_anim_set_playback_duration(&anim, VQEAF_M3_MOTION_LONG * 2);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_early_apply(&anim, true);
    lv_anim_start(&anim);
    return bar;
}

int vqeaf_wallpaper_active(void)
{
    return qeaf_wallpaper.data != NULL && qeaf_wallpaper.header.w > 0;
}

lv_color_t vqeaf_wallpaper_text_color(void)
{
    if(qeaf_wallpaper.data != NULL && qeaf_wallpaper.header.w > 0)
        return lv_color_hex(0xF2FAFF); /* light ink over the dark photo */
    return vqeaf_theme_liquid_glass() ? lv_color_hex(0xFFF7FF) :
                                        VQEAF_C_TEXT;
}

static lv_obj_t *wallpaper_blob(lv_obj_t *parent, int32_t width,
                                int32_t height, int32_t x, int32_t y,
                                uint32_t top, uint32_t bottom, lv_opa_t opa)
{
    lv_obj_t *blob = lv_obj_create(parent);
    lv_obj_remove_style_all(blob);
    lv_obj_set_size(blob, width, height);
    lv_obj_set_pos(blob, x, y);
    lv_obj_set_style_radius(blob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(blob, lv_color_hex(top), 0);
    lv_obj_set_style_bg_grad_color(blob, lv_color_hex(bottom), 0);
    lv_obj_set_style_bg_grad_dir(blob, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(blob, 0, 0);
    lv_obj_set_style_bg_grad_stop(blob, 255, 0);
    lv_obj_set_style_bg_opa(blob, opa, 0);
    lv_obj_set_style_border_width(blob, 0, 0);
    lv_obj_remove_flag(blob, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(blob, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return blob;
}

void vqeaf_wallpaper(lv_obj_t *parent)
{
    const int light = vqeaf_theme_get() == VQEAF_THEME_LIGHT;
    const int glass = g_liquid_glass;
    lv_obj_t *base = lv_obj_create(parent);
    int32_t w, h, sun;
    uint32_t sky_top, sky_bottom, sun_top, sun_bottom;
    uint32_t cloud_a, cloud_b, cloud_c, ridge_a, ridge_b;
    lv_opa_t cloud_opa, ridge_opa;

    lv_obj_remove_style_all(base);
    lv_obj_set_pos(base, 0, 0);
    lv_obj_set_size(base, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(base, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(base, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(base, 0, 0);
    lv_obj_set_style_bg_grad_stop(base, 255, 0);
    lv_obj_remove_flag(base, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_update_layout(base);
    w = lv_obj_get_width(base);
    h = lv_obj_get_height(base);
    if(w <= 0) w = VQEAF_SCREEN_W;
    if(h <= 0) h = VQEAF_SCREEN_H;

    /* Real photo wallpaper (generated from a user-supplied image, RGB565 at
     * the exact UI canvas size). It covers the whole content box; the vector
     * gradient below stays as the fallback for a zero-sized descriptor. */
    if(qeaf_wallpaper.data != NULL && qeaf_wallpaper.header.w > 0 &&
       qeaf_wallpaper.header.h > 0) {
        lv_obj_t *photo = lv_image_create(base);
        lv_image_set_src(photo, &qeaf_wallpaper);
        lv_obj_set_size(photo, w, h);
        /* "Cover": scale uniformly by the larger axis ratio so the picture
         * always fills the box; LVGL clips the overflow to the parent base.
         * This keeps the black-hole ring centred in portrait and landscape
         * instead of stretching it into an oval. */
        {
            int32_t iw = (int32_t)qeaf_wallpaper.header.w;
            int32_t ih = (int32_t)qeaf_wallpaper.header.h;
            uint32_t zoom = 256;
            if(iw > 0 && ih > 0) {
                uint32_t zx = (uint32_t)(((int64_t)w * 256 + iw / 2) / iw);
                uint32_t zy = (uint32_t)(((int64_t)h * 256 + ih / 2) / ih);
                zoom = zx > zy ? zx : zy;
                if(zoom < 256) zoom = 256;
            }
            lv_image_set_scale_x(photo, zoom);
            lv_image_set_scale_y(photo, zoom);
        }
        lv_obj_align(photo, LV_ALIGN_CENTER, 0, 0);
        lv_obj_remove_flag(photo,
            LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(photo, LV_OBJ_FLAG_GESTURE_BUBBLE);

        /* Contrast veil. The photo is mostly near-black; in the light theme
         * dark text on it was unreadable. A translucent white veil lifts the
         * backdrop for dark text; in dark themes a black veil keeps light
         * text legible. The veil sits above the photo but below all page
         * content and never takes focus or gestures. */
        {
            lv_obj_t *veil = lv_obj_create(base);
            lv_obj_remove_style_all(veil);
            lv_obj_set_size(veil, lv_pct(100), lv_pct(100));
            lv_obj_set_pos(veil, 0, 0);
            lv_obj_set_style_bg_color(veil, light ? lv_color_white() :
                                                  lv_color_black(), 0);
            lv_obj_set_style_bg_opa(veil,
                (lv_opa_t)(light ? (g_liquid_glass ? 50 : 58) :
                                   (g_liquid_glass ? 34 : 46)), 0);
            lv_obj_remove_flag(veil, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(veil, LV_OBJ_FLAG_GESTURE_BUBBLE);
            lv_obj_remove_flag(veil, LV_OBJ_FLAG_CLICKABLE);
        }
        return;
    }

    if(glass) {
        sky_top = 0x392055; sky_bottom = 0xF3A18F;
        sun_top = 0xF7C4D1; sun_bottom = 0xF5A581;
        cloud_a = 0x82589E; cloud_b = 0xD17DB2; cloud_c = 0xF0B4A0;
        ridge_a = 0x81688E; ridge_b = 0x553F6E;
        cloud_opa = 58; ridge_opa = 48;
    } else if(light) {
        sky_top = 0xD9E0E1; sky_bottom = 0xE6E9E7;
        sun_top = 0xEDF0EE; sun_bottom = 0xC8D0CF;
        cloud_a = 0x87999C; cloud_b = 0xAAB6B6; cloud_c = 0xD4DAD8;
        ridge_a = 0x809092; ridge_b = 0xA4B0AE;
        cloud_opa = 17; ridge_opa = 13;
    } else {
        sky_top = 0x10232F; sky_bottom = 0x111D28;
        sun_top = 0x356B74; sun_bottom = 0x614B79;
        cloud_a = 0x1C6669; cloud_b = 0x285878; cloud_c = 0x72557F;
        ridge_a = 0x1E3D49; ridge_b = 0x162C38;
        cloud_opa = 52; ridge_opa = 72;
    }

    lv_obj_set_style_bg_color(base, lv_color_hex(sky_top), 0);
    lv_obj_set_style_bg_grad_color(base, lv_color_hex(sky_bottom), 0);

    /* Keep the background code-drawn: no image widget, decoder, or bitmap
     * asset is required for the fog and layered landscape. */
    sun = w * 56 / 100;
    wallpaper_blob(base, sun, sun, w * 51 / 100, h * 18 / 100,
                   sun_top, sun_bottom, glass ? 42 : (light ? 17 : 30));

    /* Small cloud forms keep the middle of the screen calm for text and
     * widgets. Their rounded ends stay inside the canvas instead of making
     * full-width horizontal bands. */
    wallpaper_blob(base, w * 62 / 100, h * 8 / 100, -w * 8 / 100,
                   h * 42 / 100, cloud_a, cloud_b,
                   (lv_opa_t)(cloud_opa * 3 / 5));
    wallpaper_blob(base, w * 46 / 100, h * 7 / 100, w * 55 / 100,
                   h * 54 / 100, cloud_b, cloud_c,
                   (lv_opa_t)(cloud_opa / 2));
    wallpaper_blob(base, w * 53 / 100, h * 8 / 100, w * 7 / 100,
                   h * 66 / 100, cloud_c, cloud_a,
                   (lv_opa_t)(cloud_opa * 2 / 5));

    /* Two broad ellipses, cropped by the display, form distant soft hills. */
    wallpaper_blob(base, w * 180 / 100, h * 56 / 100, -w * 40 / 100,
                   h * 73 / 100, ridge_a, ridge_b, ridge_opa);
    wallpaper_blob(base, w * 170 / 100, h * 48 / 100, -w * 15 / 100,
                   h * 82 / 100, ridge_b, sky_bottom,
                   (lv_opa_t)(ridge_opa * 3 / 4));

    /* Soft, layered hills echo the misty references without adding a bitmap
     * or image decoder to the shell. */
    wallpaper_blob(base, w * 145 / 100, h * 27 / 100, -w * 24 / 100,
                   h * 60 / 100, ridge_a, cloud_b,
                   glass ? 21 : (light ? 13 : 34));
    wallpaper_blob(base, w * 155 / 100, h * 24 / 100, -w * 42 / 100,
                   h * 70 / 100, ridge_b, sky_bottom,
                   glass ? 34 : (light ? 15 : 50));
}
