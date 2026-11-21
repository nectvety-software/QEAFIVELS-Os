#!/bin/bash
S=/mnt/d/QeafivelsOfficial/QeafdropOs/out/reference/stock-violet/system.img
debugfs -R 'cat /system/build.prop' "$S" 2>/dev/null | grep -E 'ro.build.version|ro.build.fingerprint|ro.build.id|ro.system.build' | head -20
