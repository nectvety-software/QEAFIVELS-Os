#!/bin/bash
cd /mnt/d/QEAFIVELS-Os
export PYTHONIOENCODING=utf-8
echo "=== wifi up"
python temp/probe_device.py --cmd "wifi up" 2>&1 | tail -2
for t in 5 10 15 20 25; do
  sleep 5
  echo "=== t=$t"
  python temp/probe_device.py --cmd "wifi log" 2>&1 | grep -E "cnss|WLFW|waiting|QMI|served|registering" | head -8
done
