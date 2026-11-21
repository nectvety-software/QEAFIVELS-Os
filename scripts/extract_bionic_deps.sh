#!/bin/bash
# Extract bionic linker + the shared-library closure needed by cnss-daemon
# from the stock system.img/vendor.img into a staging dir for the QEAF ramdisk.
set -u
S=/mnt/d/QeafivelsOfficial/QeafdropOs/out/reference/stock-violet/system.img
V=/mnt/d/QeafivelsOfficial/QeafdropOs/out/reference/stock-violet/vendor.img
D=/root/qeaf-bionic
STAGE=$D/stage
rm -rf "$STAGE"
mkdir -p "$STAGE/system/bin" "$STAGE/system/lib64" "$STAGE/vendor/lib64"

realpath_img() {
  local img=$1 path=$2 out
  out=$(debugfs -R "stat $path" "$img" 2>/dev/null | grep 'Fast link dest:' | sed 's/.*Fast link dest: "//; s/"$//')
  if [ -n "$out" ]; then echo "$out"; else echo "$path"; fi
}

extract_one() {
  local name=$1 img=$2 dir=$3 src dest
  src=$(realpath_img "$img" "$dir/$name")
  dest="$STAGE$dir/$name"
  mkdir -p "$(dirname "$dest")"
  debugfs -R "cat $src" "$img" > "$dest" 2>/dev/null
  if [ ! -s "$dest" ]; then echo "MISS $name ($dir -> $src)"; return 1; fi
  echo "got  $name ($(stat -c%s "$dest") bytes, $dir)"
  return 0
}

# linker: /system/bin/linker64 is a symlink into the runtime APEX
L=$(realpath_img "$S" /system/bin/linker64)
echo "linker source: $L"
debugfs -R "cat $L" "$S" > "$STAGE/system/bin/linker64" 2>/dev/null
chmod 755 "$STAGE/system/bin/linker64"
ls -l "$STAGE/system/bin/linker64"

QUEUE="libcutils.so liblog.so libqmi_cci.so libqmi_common_so.so libnl.so libcld80211.so libperipheral_client.so libmdmdetect.so libqmiservices.so libc++.so libc.so libm.so libdl.so"
SEEN=""
for round in 1 2 3; do
  NEXT=""
  for lib in $QUEUE; do
    case " $SEEN " in *" $lib "*) continue;; esac
    SEEN="$SEEN $lib"
    if extract_one "$lib" "$S" /system/lib64; then :; else
      extract_one "$lib" "$V" /lib64 || true
    fi
    f="$STAGE/system/lib64/$lib"
    [ -f "$f" ] || f="$STAGE/vendor/lib64/$lib"
    if [ -f "$f" ]; then
      for dep in $(readelf -d "$f" 2>/dev/null | grep NEEDED | sed 's/.*\[\(.*\)\]/\1/'); do
        NEXT="$NEXT $dep"
      done
    fi
  done
  QUEUE="$NEXT"
  [ -n "$QUEUE" ] || break
done

echo "== staged"
find "$STAGE" -type f -printf '%p %s\n' | sort
du -sh "$STAGE"
