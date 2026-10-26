#!/bin/sh
# 在 macOS/Linux 上用 llvm-mingw 交叉编译 CountDay.exe（不需要 CMake）
# 用法: LLVM_MINGW=/path/to/llvm-mingw sh tools/build_mingw.sh
set -e
cd "$(dirname "$0")/.."

: "${LLVM_MINGW:?请设置 LLVM_MINGW 为 llvm-mingw 解压目录}"
ARCH="${ARCH:-x86_64}"
CXX="$LLVM_MINGW/bin/$ARCH-w64-mingw32-clang++"
RC="$LLVM_MINGW/bin/$ARCH-w64-mingw32-windres"
OUT=build-mingw
# -Dsmall=char：模拟 MSVC 下 rpcndr.h 的同名宏，提前发现命名冲突
mkdir -p "$OUT"

DEFS="-Dsmall=char -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -DNTDDI_VERSION=0x0A000003"

"$RC" -I src -I res res/app.rc -O coff -o "$OUT/app.res.o"

"$CXX" -std=c++17 -Os -Wall -Wextra -fno-rtti $DEFS \
  src/*.cpp "$OUT/app.res.o" \
  -municode -mwindows -static -s \
  -ld2d1 -ldwrite -lcomctl32 -lshcore -lshell32 -luser32 -lgdi32 -ladvapi32 -lole32 -lwtsapi32 -luxtheme \
  -o "$OUT/CountDay.exe"

ls -l "$OUT/CountDay.exe"
