#!/bin/bash
# Fetch WCN3990 ath10k firmware from linux-firmware for the mainline test.
set -u
DEST=/mnt/d/QEAFIVELS-Os/build/violet/firmware-mainline/ath10k/WCN3990/hw1.0
mkdir -p "$DEST"
BASE=https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/ath10k/WCN3990/hw1.0
for f in firmware-5.bin board-2.bin; do
  code=$(curl -sSL --max-time 120 -w '%{http_code}' -o "$DEST/$f" "$BASE/$f")
  echo "$f $code $(stat -c%s "$DEST/$f" 2>/dev/null || echo 0)"
done
