#!/usr/bin/env bash
# Native MiniOS ELF, with SDK and all outputs isolated from its default build.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MINIOS="${MINIOS:-$HOME/minios-m}"
OUT="${1:-$ROOT/build/minios/ArkLink}"
SDK_BUILD="${MINIOS_SDK_BUILD:-$ROOT/build/minios/sdk-build}"
mkdir -p "$(dirname "$OUT")" "$SDK_BUILD"
SDK_BUILD="$(cd "$SDK_BUILD" && pwd)"
make -C "$MINIOS" BUILD="$SDK_BUILD" "$SDK_BUILD/sdk/libminios.a" "$SDK_BUILD/sdk/crt0.o"
objects=()
while IFS= read -r source; do
    relative="${source#"$ROOT/ArkLink/src/"}"
    object="$(dirname "$OUT")/arklink-objects/${relative%.c}.o"
    mkdir -p "$(dirname "$object")"
    gcc -m64 -mno-red-zone -msse2 -fcf-protection=none -fno-pic -fno-pie \
        -ffreestanding -fno-stack-protector -std=gnu11 -Wall -Wextra -O2 \
        -I"$MINIOS/sdk/include" -I"$ROOT/ArkLink/include" -c "$source" -o "$object"
    objects+=("$object")
done < <(python3 -c 'from pathlib import Path;import sys;print("\n".join(map(str,sorted(Path(sys.argv[1]).rglob("*.c")))))' "$ROOT/ArkLink/src")
ld -n -z max-page-size=0x1000 --no-warn-rwx-segments -T "$MINIOS/sdk/user.ld" \
    -o "$OUT" "$SDK_BUILD/sdk/crt0.o" "${objects[@]}" "$SDK_BUILD/sdk/libminios.a"
printf 'Built MiniOS ArkLink: %s\n' "$OUT"
