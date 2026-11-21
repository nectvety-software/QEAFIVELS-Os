#!/bin/bash
set -e
D=/root/violet-fw
mkdir -p "$D"
URL=https://github.com/XiaomiFirmwareUpdaterReleases/firmware_xiaomi_violet/releases/download/weekly-31.10.2020/fw_violet_miui_VIOLET_20.10.30_b005a89340_10.0.zip
if [ ! -s "$D/violet-fw.zip" ]; then
  curl -sSL --max-time 600 -o "$D/violet-fw.zip" "$URL"
fi
ls -l "$D/violet-fw.zip"
unzip -l "$D/violet-fw.zip" | head -40
