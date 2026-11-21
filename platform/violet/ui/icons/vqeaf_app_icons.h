/* SPDX-License-Identifier: MIT */
/* Compatibility enum; built-in app artwork is rendered with symbol glyphs. */
#ifndef VQEAF_APP_ICONS_H
#define VQEAF_APP_ICONS_H

#include "lvgl.h"

typedef enum {
    VQEAF_APP_ICON_HOME = 0,
    VQEAF_APP_ICON_APPS = 1,
    VQEAF_APP_ICON_CONTROL = 2,
    VQEAF_APP_ICON_SETTINGS = 3,
    VQEAF_APP_ICON_PHONE = 4,
    VQEAF_APP_ICON_MESSAGES = 5,
    VQEAF_APP_ICON_CONTACTS = 6,
    VQEAF_APP_ICON_BROWSER = 7,
    VQEAF_APP_ICON_CAMERA = 8,
    VQEAF_APP_ICON_GALLERY = 9,
    VQEAF_APP_ICON_MUSIC = 10,
    VQEAF_APP_ICON_VIDEO = 11,
    VQEAF_APP_ICON_FILES = 12,
    VQEAF_APP_ICON_STORE = 13,
    VQEAF_APP_ICON_GAMES = 14,
    VQEAF_APP_ICON_CALCULATOR = 15,
    VQEAF_APP_ICON_RECORDER = 16,
    VQEAF_APP_ICON_CALENDAR = 17,
    VQEAF_APP_ICON_NOTES = 18,
    VQEAF_APP_ICON_WEATHER = 19,
    VQEAF_APP_ICON_SECURITY = 20,
    VQEAF_APP_ICON_CLOCK = 21,
    VQEAF_APP_ICON_UPDATE = 22,
    VQEAF_APP_ICON_STORAGE = 23,
    VQEAF_APP_ICON_ABOUT = 24,
    VQEAF_APP_ICON_SHARE = 25,
    VQEAF_APP_ICON_TOOLS = 26,
    VQEAF_APP_ICON_VQEAF = 27,
    VQEAF_APP_ICON_QEAPP = 28,
    VQEAF_APP_ICON_COUNT
} vqeaf_app_icon_t;

/* Deprecated compatibility shim; currently always NULL. */
const lv_image_dsc_t *vqeaf_app_icon(int icon);

#endif /* VQEAF_APP_ICONS_H */
