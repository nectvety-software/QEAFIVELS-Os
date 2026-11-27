#!/usr/bin/env bash
set -euo pipefail
cd /tmp
curl -fL --retry 3 https://dl.google.com/android/repository/android-ndk-r30-linux.zip -o android-ndk-r30-linux.zip
echo "SHA1:"
sha1sum android-ndk-r30-linux.zip
echo "5107f898313790e449e87eee2183d9a20602dee9  android-ndk-r30-linux.zip" | sha1sum -c -
unzip -q android-ndk-r30-linux.zip -d /opt
chmod -R a+rx /opt/android-ndk-r30/toolchains/llvm/prebuilt/linux-x86_64/bin
/opt/android-ndk-r30/toolchains/llvm/prebuilt/linux-x86_64/bin/clang --version | head -2
