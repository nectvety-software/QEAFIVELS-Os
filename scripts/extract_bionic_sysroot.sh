#!/bin/bash
# Extract Bionic runtime (APEX) + needed libs from the stock dumps into ramdisk staging.
set -u
S=/mnt/d/QeafivelsOfficial/QeafdropOs/out/reference/stock-violet/system.img
V=/mnt/d/QeafivelsOfficial/QeafdropOs/out/reference/stock-violet/vendor.img
DEST=/mnt/d/QEAFIVELS-Os/build/violet/bionic-sysroot
rm -rf "$DEST"
mkdir -p "$DEST/apex/com.android.runtime/bin" \
         "$DEST/apex/com.android.runtime/lib64/bionic" \
         "$DEST/system/lib64" "$DEST/vendor/lib64"

cat_file() { # img, path, dest
  debugfs -R "cat $2" "$1" > "$3" 2>/dev/null
  [ -s "$3" ] || { echo "MISS $2"; rm -f "$3"; return 1; }
  return 0
}

echo "== APEX runtime"
for f in linker64 linker linkerconfig; do
  cat_file "$S" "/system/apex/com.android.runtime/bin/$f" \
    "$DEST/apex/com.android.runtime/bin/$f" && echo "  bin/$f"
done
for f in libc.so libdl.so libdl_android.so libm.so; do
  cat_file "$S" "/system/apex/com.android.runtime/lib64/bionic/$f" \
    "$DEST/apex/com.android.runtime/lib64/bionic/$f" && echo "  bionic/$f"
done
for f in ld-android.so libbase.so libc++.so libc_malloc_debug.so liblzma.so libunwindstack.so; do
  cat_file "$S" "/system/apex/com.android.runtime/lib64/$f" \
    "$DEST/apex/com.android.runtime/lib64/$f" && echo "  lib64/$f"
done

echo "== system libs"
for f in libcutils.so liblog.so libc++.so libbase.so libnl.so libz.so libzstd.so; do
  cat_file "$S" "/system/lib64/$f" "$DEST/system/lib64/$f" && echo "  $f"
done

echo "== vendor QMI libs"
for f in libqmi_cci.so libqmi_common_so.so libcld80211.so libperipheral_client.so libmdmdetect.so libqmiservices.so; do
  cat_file "$V" "/lib64/$f" "$DEST/vendor/lib64/$f" && echo "  $f"
done

echo "== sizes"
du -sh "$DEST"
find "$DEST" -type f -printf '%P %s\n' | sort
