#!/bin/bash
for r in pd-mapper qrtr rmtfs; do
  echo "== $r"
  curl -sSL "https://api.github.com/repos/linux-msm/$r/contents" |
    grep -o '"name": "[^"]*"' | head -25
done
echo '== aarch64-laptops misc/lenovo-yoga-c630'
curl -sSL 'https://api.github.com/repos/aarch64-laptops/build/contents/misc/lenovo-yoga-c630' |
  grep -o '"name": "[^"]*"' | head -25
