#!/bin/bash
export LD_LIBRARY_PATH=/root/libxkb/usr/lib/x86_64-linux-gnu
export SLINT_BACKEND=winit-software
xvfb-run -a -s "-screen 0 393x852x24" timeout 8s /root/qeaf-slint-target/release/qeaf-shell
rc=$?
echo TEST_RC=$rc
exit 0
