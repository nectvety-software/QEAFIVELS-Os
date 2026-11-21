#!/bin/bash
# Fetch upstream Wi-Fi driver/service documentation for offline reference.
set -u
DEST=/mnt/d/QEAFIVELS-Os/docs/reference/wifi
mkdir -p "$DEST"

fetch() {
  local url=$1 file=$2
  local code
  code=$(curl -sSL --max-time 60 -w '%{http_code}' -o "$DEST/$file" "$url")
  printf '%-34s %s  %s bytes\n' "$file" "$code" "$(stat -c%s "$DEST/$file" 2>/dev/null || echo 0)"
}

fetch https://raw.githubusercontent.com/ScYurii/wlfwd/main/README.md wlfwd-README.md
fetch https://raw.githubusercontent.com/linux-msm/pd-mapper/master/README.md pd-mapper-README.md
fetch https://raw.githubusercontent.com/linux-msm/qrtr/master/README.md qrtr-README.md
fetch https://raw.githubusercontent.com/linux-msm/tqftpserv/master/README.md tqftpserv-README.md
fetch https://raw.githubusercontent.com/linux-msm/rmtfs/master/README.md rmtfs-README.md
fetch https://raw.githubusercontent.com/torvalds/linux/master/drivers/soc/qcom/qcom_pd_mapper.c qcom_pd_mapper.c.txt
fetch https://raw.githubusercontent.com/torvalds/linux/master/drivers/soc/qcom/pdr_internal.h pdr_internal.h.txt
fetch https://www.kernel.org/doc/Documentation/devicetree/bindings/net/wireless/qcom,ath10k.txt qcom-ath10k-dt-binding.txt
fetch https://raw.githubusercontent.com/aarch64-laptops/build/main/misc/lenovo-yoga-c630/wifi/README.md aarch64-laptops-wifi-README.md
