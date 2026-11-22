#!/bin/bash
set -u
cd /mnt/d/QEAFIVELS-Os/build/firmware-full
echo "--- xz non-hlos ---"
tar -cJf /tmp/nonhlos.tar.xz non-hlos && ls -l /tmp/nonhlos.tar.xz
echo "--- xz dspso ---"
tar -cJf /tmp/dspso.tar.xz dspso && ls -l /tmp/dspso.tar.xz
echo "--- total ---"
du -cb /tmp/nonhlos.tar.xz /tmp/dspso.tar.xz | tail -1
