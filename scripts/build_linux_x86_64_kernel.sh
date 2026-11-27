#!/bin/sh
set -eu

work=$1
version=$2
source_url=$3
source_sha256=$4
fragment=$5
build=$6

case "$work" in
    /tmp/qeafivels-linux-x86_64*) ;;
    *) echo "unsafe WSL work path: $work" >&2; exit 1 ;;
esac

for tool in curl tar xz make gcc ld sha256sum cut cp mv; do
    command -v "$tool" >/dev/null || { echo "missing $tool" >&2; exit 1; }
done

source="$work/linux-$version"
output="$work/out"
archive="$work/linux-$version.tar.xz"
mkdir -p "$work" "$build"

if [ ! -f "$archive" ]; then
    curl -fL --retry 3 -o "$archive.part" "$source_url"
    mv "$archive.part" "$archive"
fi
actual=$(sha256sum "$archive" | cut -d' ' -f1)
[ "$actual" = "$source_sha256" ] || {
    echo "source hash mismatch: $actual" >&2
    exit 1
}

if [ ! -f "$source/Makefile" ]; then
    tar -xJf "$archive" -C "$work"
fi
# mrproper does not restore edited tracked files; compare the tree to the
# verified archive before attributing the build to the pinned source hash.
tar -dJf "$archive" -C "$work"
make -C "$source" mrproper

mkdir -p "$output"
make -C "$source" O="$output" mrproper
make -C "$source" O="$output" x86_64_defconfig
"$source/scripts/kconfig/merge_config.sh" -m "$output/.config" "$fragment"
make -C "$source" O="$output" olddefconfig
# Linux 4.14 olddefconfig restores retpoline/ORC defaults after a fragment
# disables them. Pin the compatibility choices after dependency resolution.
"$source/scripts/config" --file "$output/.config" \
    -d RETPOLINE -d STACK_VALIDATION -d UNWINDER_ORC -d MODULES \
    -d LOCALVERSION_AUTO --set-str LOCALVERSION "" \
    -e UNWINDER_FRAME_POINTER
make -C "$source" O="$output" silentoldconfig
for symbol in RETPOLINE STACK_VALIDATION UNWINDER_ORC MODULES; do
    if grep -q "^CONFIG_${symbol}=y$" "$output/.config"; then
        echo "unexpected enabled compatibility symbol: $symbol" >&2
        exit 1
    fi
done
grep -q '^CONFIG_UNWINDER_FRAME_POINTER=y$' "$output/.config"
grep -q '^# CONFIG_MODULES is not set$' "$output/.config"
grep -q '^CONFIG_LOCALVERSION=""$' "$output/.config"
release=$(make -s -C "$source" O="$output" kernelrelease)
[ "$release" = "$version" ] || {
    echo "unexpected source release: $release" >&2
    exit 1
}

export LC_ALL=C TZ=UTC SOURCE_DATE_EPOCH=1610478720
export KBUILD_BUILD_TIMESTAMP='Tue Jan 12 19:12:00 UTC 2021'
export KBUILD_BUILD_HOST=qeaf-qemu KBUILD_BUILD_USER=qeaf
# GCC 13 reports a false positive in Linux 4.14 objtool; keep other warnings fatal.
export EXTRA_CFLAGS=-Wno-error=use-after-free
make -C "$source" O="$output" HOSTCC='gcc -Wno-error=use-after-free' \
    -j"$(nproc)" bzImage

cp "$output/arch/x86/boot/bzImage" "$build/bzImage"
cp "$output/.config" "$build/kernel.config"
cp "$output/System.map" "$build/System.map"
printf '%s\n' "$release" > "$build/kernel.release"
