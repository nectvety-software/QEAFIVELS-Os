#!/usr/bin/env python3
"""Compatibility stub: built-in app icons now use native symbol glyphs.

The system launcher no longer embeds generated LVGL image descriptors.
User-provided picture/file content remains handled by the image viewer.
"""
print("Static LVGL app icons are disabled; use vqeaf_app_icons.c glyph fallback.")
