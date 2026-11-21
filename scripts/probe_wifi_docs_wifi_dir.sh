#!/bin/bash
curl -sSL 'https://api.github.com/repos/aarch64-laptops/build/contents/misc/lenovo-yoga-c630/wifi' |
  grep -o '"name": "[^"]*"'
