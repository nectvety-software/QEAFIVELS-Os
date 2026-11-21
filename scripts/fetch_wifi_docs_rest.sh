#!/bin/bash
DEST=/mnt/d/QEAFIVELS-Os/docs/reference/wifi
mkdir -p "$DEST"
fetch() {
  local url=$1 file=$2 code
  code=$(curl -sSL --max-time 60 -w '%{http_code}' -o "$DEST/$file" "$url")
  printf '%-38s %s  %s bytes\n' "$file" "$code" "$(stat -c%s "$DEST/$file" 2>/dev/null || echo 0)"
}
fetch https://raw.githubusercontent.com/linux-msm/pd-mapper/master/LICENSE pd-mapper-LICENSE.txt
fetch https://raw.githubusercontent.com/linux-msm/qrtr/master/LICENSE qrtr-LICENSE.txt
fetch https://raw.githubusercontent.com/linux-msm/rmtfs/master/LICENSE rmtfs-LICENSE.txt
fetch https://raw.githubusercontent.com/aarch64-laptops/build/master/misc/lenovo-yoga-c630/wifi/README.md aarch64-laptops-wifi-README.md
fetch https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/soc/qcom/pdr_internal.h pdr_internal.h.txt
