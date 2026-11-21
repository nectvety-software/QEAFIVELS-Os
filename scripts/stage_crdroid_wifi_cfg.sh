#!/bin/bash
DEST=/mnt/d/QEAFIVELS-Os/build/violet/config-wifi
mkdir -p "$DEST"
cp /tmp/crdroid-WCNSS_qcom_cfg.ini "$DEST/WCNSS_qcom_cfg.ini"
cp /tmp/crdroid-p2p_supplicant_overlay.conf "$DEST/p2p_supplicant_overlay.conf"
cp /tmp/crdroid-wpa_supplicant_overlay.conf "$DEST/wpa_supplicant_overlay.conf"
sha256sum "$DEST"/*
