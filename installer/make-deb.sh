#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 MOONLIGHT
#
# 把已经编译好的 disktest 打成一个 Debian 包（Ubuntu / Debian 通用）。
#
# 用法（在仓库根目录或任意位置跑，参数都给绝对/相对路径即可）：
#   installer/make-deb.sh [--build-dir build-linux] [--src-root .] [--out build]
#                         [--depends "libc6 (>= 2.34), ..."]
#
# 说明：
#   * 不重新编译，直接取 <build-dir>/dist/disktest（和 `cmake --build ... --target package`
#     用的同一份产物），strip 之后装进 /usr/bin。
#   * 目标架构由二进制自己判断（x86-64 → amd64，aarch64 → arm64），
#     所以 amd64 主机上的交叉编译产物也能直接打成 arm64 的 deb。
#   * 依赖（Depends）用 dpkg-shlibdeps 从 ELF 的 NEEDED 反查，不手写包名。
#     交叉编译的产物（arm64 在 amd64 主机上）没有对应的库可查，这时用
#     --depends "..." 显式给一份：包名与版本约束跟同版本 amd64 包一致。
#   * 需要 dpkg-deb / dpkg-shlibdeps（Debian 系自带），不需要 debhelper。
set -euo pipefail

# 别把 umask 带进包里：dpkg 里目录是 755、文件是 644，002 会打出 775/664。
umask 022

BUILD_DIR="build-linux"
SRC_ROOT="."
OUT_DIR="build"
DEPENDS_OVERRIDE=""

while [ $# -gt 0 ]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --src-root)  SRC_ROOT="$2";  shift 2 ;;
        --out)       OUT_DIR="$2";   shift 2 ;;
        --depends)   DEPENDS_OVERRIDE="$2"; shift 2 ;;
        -h|--help)   awk 'NR > 1 { if ($0 ~ /^set /) exit; sub(/^# ?/, ""); print }' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

PKG="disktest"
VERSION=$(sed -n 's/^ *VERSION \([0-9][0-9.]*\).*/\1/p' "$SRC_ROOT/CMakeLists.txt" | head -1)
[ -n "$VERSION" ] || { echo "cannot read version from CMakeLists.txt" >&2; exit 1; }
REVISION="1"
DEBVERSION="${VERSION}-${REVISION}"

BIN="$BUILD_DIR/dist/disktest"
[ -x "$BIN" ] || { echo "missing $BIN (run: cmake --build $BUILD_DIR --target package)" >&2; exit 1; }

# 架构：看 ELF 头，别用 dpkg --print-architecture（交叉编译时会错）
case "$(file -b "$BIN")" in
    *x86-64*)  ARCH="amd64"; STRIP="strip" ;;
    *aarch64*) ARCH="arm64"; STRIP="aarch64-linux-gnu-strip" ;;
    *) echo "unsupported architecture: $(file -b "$BIN")" >&2; exit 1 ;;
esac
command -v "$STRIP" >/dev/null || { echo "missing strip tool: $STRIP" >&2; exit 1; }

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
ROOT="$WORK/root"
mkdir -p "$ROOT/DEBIAN" \
         "$ROOT/usr/bin" \
         "$ROOT/usr/share/doc/$PKG/licenses" \
         "$ROOT/usr/share/applications" \
         "$ROOT/usr/share/pixmaps"

# ---------------------------------------------------------------- 文件
install -m 0755 "$BIN" "$ROOT/usr/bin/$PKG"
"$STRIP" --strip-unneeded "$ROOT/usr/bin/$PKG"

install -m 0644 "$SRC_ROOT/README.md"              "$ROOT/usr/share/doc/$PKG/README.md"
install -m 0644 "$SRC_ROOT/THIRD-PARTY-NOTICES.md" "$ROOT/usr/share/doc/$PKG/THIRD-PARTY-NOTICES.md"
install -m 0644 "$SRC_ROOT/LICENSE"                "$ROOT/usr/share/doc/$PKG/licenses/LICENSE"
install -m 0644 "$SRC_ROOT/licenses/Qt-LGPLv3.txt" "$ROOT/usr/share/doc/$PKG/licenses/Qt-LGPLv3.txt"
install -m 0644 "$SRC_ROOT/licenses/fio-GPLv2.txt" "$ROOT/usr/share/doc/$PKG/licenses/fio-GPLv2.txt"

# 图标：母版只有 128×128，按 hicolor 的标准尺寸各缩一份（不放大）。
ICON="$SRC_ROOT/resources/logo-window.png"
for SIZE in 16 22 24 32 48 64 128; do
    DIR="$ROOT/usr/share/icons/hicolor/${SIZE}x${SIZE}/apps"
    mkdir -p "$DIR"
    if command -v convert >/dev/null 2>&1 && [ "$SIZE" != "128" ]; then
        convert "$ICON" -resize "${SIZE}x${SIZE}" "$DIR/$PKG.png"
        chmod 0644 "$DIR/$PKG.png"
    else
        install -m 0644 "$ICON" "$DIR/$PKG.png"
    fi
done
install -m 0644 "$ICON" "$ROOT/usr/share/pixmaps/$PKG.png"

# ---------------------------------------------------------------- copyright
cat > "$ROOT/usr/share/doc/$PKG/copyright" <<EOF
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: disktest_fio
Upstream-Contact: MOONLIGHT <flowerzicon@outlook.com>
Source: https://github.com/FlowerZiCON/disktest_fio
Comment: 本包只包含 GTK-free 的 Qt 程序本体；Qt 运行库与 fio 由系统提供。

Files: *
Copyright: 2026 MOONLIGHT
License: GPL-3.0-or-later
 This program is free software: you can redistribute it and/or modify it under
 the terms of the GNU General Public License as published by the Free Software
 Foundation, either version 3 of the License, or (at your option) any later
 version.
 .
 This program is distributed in the hope that it will be useful, but WITHOUT ANY
 WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 .
 On Debian systems, the complete text of the GNU General Public License version 3
 can be found in /usr/share/common-licenses/GPL-3.

Files: usr/share/doc/disktest/licenses/Qt-LGPLv3.txt
Copyright: The Qt Company
License: LGPL-3.0
 Qt is dynamically linked.  On Debian systems the complete text of the GNU Lesser
 General Public License version 3 can be found in
 /usr/share/common-licenses/LGPL-3.

Files: usr/share/doc/disktest/licenses/fio-GPLv2.txt
Copyright: Jens Axboe and contributors
License: GPL-2.0
 fio is invoked as an external program, not linked.  On Debian systems the
 complete text of the GNU General Public License version 2 can be found in
 /usr/share/common-licenses/GPL-2.
EOF

# ---------------------------------------------------------------- changelog
cat > "$WORK/changelog" <<EOF
$PKG ($DEBVERSION) unstable; urgency=medium

  * New upstream release $VERSION.
  * UI in Simplified Chinese, Traditional Chinese and English (--lang, Help > Language).
  * Language choice is stored next to the executable, falling back to ~/.config.
  * Windows installer added upstream; Linux distribution is this tarball / deb only.

 -- MOONLIGHT <flowerzicon@outlook.com>  $(date -R)
EOF
gzip -9n -c "$WORK/changelog" > "$ROOT/usr/share/doc/$PKG/changelog.Debian.gz"
chmod 0644 "$ROOT/usr/share/doc/$PKG/changelog.Debian.gz" \
           "$ROOT/usr/share/doc/$PKG/copyright"

# ---------------------------------------------------------------- desktop
cat > "$ROOT/usr/share/applications/$PKG.desktop" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=Disk Test
Name[zh_CN]=磁盘测试
Name[zh_TW]=磁碟測試
GenericName=Disk Reliability Tester
GenericName[zh_CN]=硬盘可靠性测试工具
GenericName[zh_TW]=硬碟可靠性測試工具
Comment=Fill a partition with test files, read it back and verify, to find bad blocks
Comment[zh_CN]=把分区写满再读回逐块校验，用来发现坏块和静默数据损坏
Comment[zh_TW]=把分割區寫滿再讀回逐塊校驗，用來發現壞區塊和靜默資料損壞
Exec=$PKG
Icon=$PKG
Terminal=false
StartupNotify=true
StartupWMClass=$PKG
Categories=Utility;System;
Keywords=disk;ssd;hdd;fio;verify;badblocks;smart;
EOF

# ---------------------------------------------------------------- Depends
# dpkg-shlibdeps 需要一个 debian/control 才认账；这里造一个最小的，只要它算出来的
# shlibs:Depends，不生成完整源码包。
SHLIB_DIR="$WORK/shlibdeps"
mkdir -p "$SHLIB_DIR/debian"
cat > "$SHLIB_DIR/debian/control" <<EOF
Source: $PKG
Priority: optional
Maintainer: MOONLIGHT <flowerzicon@outlook.com>
Standards-Version: 4.6.0

Package: $PKG
Architecture: any
Depends: \${shlibs:Depends}
Description: disk reliability and performance tester
 temporary control file, only used to compute Depends
EOF
SHLIBS="$DEPENDS_OVERRIDE"
if [ -z "$SHLIBS" ]; then
    SHLIBS=$(cd "$SHLIB_DIR" && dpkg-shlibdeps -O "$ROOT/usr/bin/$PKG" 2>/dev/null \
             | sed -n 's/^shlibs:Depends=//p')
    [ -n "$SHLIBS" ] || {
        echo "dpkg-shlibdeps failed to compute dependencies." >&2
        echo "For a cross-built binary (e.g. arm64 on amd64) pass --depends \"...\"." >&2
        exit 1
    }
else
    echo "==> Depends given explicitly (cross-built binary): $SHLIBS"
fi

# ---------------------------------------------------------------- control
INSTALLED_SIZE=$(du -sk --exclude=DEBIAN "$ROOT" | cut -f1)
cat > "$ROOT/DEBIAN/control" <<EOF
Package: $PKG
Version: $DEBVERSION
Architecture: $ARCH
Maintainer: MOONLIGHT <flowerzicon@outlook.com>
Installed-Size: $INSTALLED_SIZE
Depends: $SHLIBS
Recommends: fio
Section: utils
Priority: optional
Homepage: https://github.com/FlowerZiCON/disktest_fio
Description: disk reliability and performance tester (Qt GUI)
 Fills the selected partition with test files, reads them back and verifies
 every block (crc32c) to find bad blocks and silent data corruption; also runs
 the standard four-item read/write throughput test on top of fio.
 .
 The UI is available in Simplified Chinese, Traditional Chinese and English
 (--lang / Help > Language).  Only file-mode tests are supported: raw devices
 such as /dev/sdX are refused on purpose.
EOF

# ---------------------------------------------------------------- build
mkdir -p "$OUT_DIR"
DEB="$OUT_DIR/${PKG}_${DEBVERSION}_${ARCH}.deb"
dpkg-deb --build --root-owner-group "$ROOT" "$DEB" >/dev/null
echo "==> $DEB"
dpkg-deb -f "$DEB" Package Version Architecture Depends Recommends Installed-Size
ls -lh "$DEB" | awk '{print "    size:", $5}'
