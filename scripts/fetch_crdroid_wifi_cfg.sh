#!/bin/bash
set -e
DEST=/mnt/d/QEAFIVELS-Os/build/violet/config-wifi
mkdir -p "$DEST"
BASE=https://raw.githubusercontent.com/crdroidandroid/android_device_xiaomi_violet/14.0/wifi
curl -sSL --max-time 60 -o "$DEST/WCNSS_qcom_cfg.ini" "$BASE/WCNSS_qcom_cfg.ini"
curl -sSL --max-time 60 -o "$DEST/p2p_supplicant_overlay.conf" "$BASE/p2p_supplicant_overlay.conf"
curl -sSL --max-time 60 -o "$DEST/wpa_supplicant_overlay.conf" "$BASE/wpa_supplicant_overlay.conf"
sha256sum "$DEST"/*
