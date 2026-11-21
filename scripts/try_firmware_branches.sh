#!/bin/bash
for b in master main; do
  printf '%s: ' "$b"
  curl -sSL --max-time 120 -o "/tmp/fw5-$b.bin" -w '%{http_code} %{size_download}\n' \
    "https://raw.githubusercontent.com/torvalds/linux-firmware/$b/ath10k/WCN3990/hw1.0/firmware-5.bin"
done
ls -l /tmp/fw5-master.bin /tmp/fw5-main.bin
