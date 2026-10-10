#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if ! command -v arm-none-eabi-gcc >/dev/null 2>&1; then
  echo "arm-none-eabi-gcc not found in PATH" >&2
  echo "Example: export PATH=\"/d/KEIL/ARM/12.2 rel1/bin:\$PATH\"" >&2
  exit 1
fi

echo "== Build firmware =="
cd "$ROOT/firmware"
mingw32-make -j"${JOBS:-4}"

echo "== Keil-style full link check (no --gc-sections) =="
makeargs=(-j"${JOBS:-4}")
mingw32-make "${makeargs[@]}" check-link

echo "== Build and run host tests =="
cd "$ROOT/tests"
mingw32-make run

echo "== Artifacts =="
ls -lh "$ROOT/firmware/build/AT32_ODrive.elf" \
       "$ROOT/firmware/build/AT32_ODrive.hex" \
       "$ROOT/firmware/build/AT32_ODrive.bin"
