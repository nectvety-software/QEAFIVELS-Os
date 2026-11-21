#!/bin/bash
curl -sSL 'https://api.github.com/repos/torvalds/linux-firmware/contents/ath10k/WCN3990/hw1.0' |
  grep -o '"name": "[^"]*"' | head -30
