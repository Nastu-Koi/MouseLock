#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# Set CXX/WINDRES/PREPROCESSOR or put a MinGW-w64 toolchain on PATH.
CXX="${CXX:-x86_64-w64-mingw32-g++}"
WINDRES="${WINDRES:-x86_64-w64-mingw32-windres}"
PREPROCESSOR="${PREPROCESSOR:-x86_64-w64-mingw32-cpp}"
mkdir -p build
"$WINDRES" --preprocessor="$PREPROCESSOR" -I native native/resources.rc -O coff -o build/resources.o
"$CXX" -std=c++17 -O2 -ffunction-sections -fdata-sections -Wall -Wextra -Wpedantic -Werror -DUNICODE -D_UNICODE -DNOMINMAX \
  -municode -mwindows -static -s -Wl,--gc-sections native/config.cpp native/locker.cpp native/theme.cpp native/notifications.cpp \
  native/settings.cpp native/main.cpp build/resources.o -o build/MouseLockGame.exe \
  -luser32 -lshell32 -ladvapi32 -lcomctl32 -lgdi32 -lgdiplus -ldwmapi -lole32 -luuid
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -DUNICODE -D_UNICODE -DNOMINMAX -municode -static \
  native/tests/config_test.cpp native/config.cpp -o build/config-test.exe -luser32
printf 'Built build/MouseLockGame.exe and build/config-test.exe\n'
