#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
TINYALSA_DIR="${1:-${TINYALSA_DIR:-$ROOT/.build/tinyalsa}}"
OUT_DIR="${OUT_DIR:-$ROOT}"
CC_BIN="${CC:-aarch64-linux-gnu-gcc}"
TINYALSA_COMMIT=e43025bbf702eb7dd8edd48c1eb50530c60f1de8

if ! git -C "$TINYALSA_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  mkdir -p "$(dirname "$TINYALSA_DIR")"
  git clone https://github.com/tinyalsa/tinyalsa.git "$TINYALSA_DIR"
fi

git -C "$TINYALSA_DIR" checkout --detach "$TINYALSA_COMMIT"
if git -C "$TINYALSA_DIR" apply --reverse --check "$ROOT/tinyalsa-xruns.patch" 2>/dev/null; then
  : # already applied
else
  git -C "$TINYALSA_DIR" apply --check "$ROOT/tinyalsa-xruns.patch"
  git -C "$TINYALSA_DIR" apply "$ROOT/tinyalsa-xruns.patch"
fi

mkdir -p "$OUT_DIR"
SOURCES=(
  "$ROOT/uac2bridge.c"
  "$TINYALSA_DIR/src/pcm.c"
  "$TINYALSA_DIR/src/pcm_hw.c"
  "$TINYALSA_DIR/src/pcm_plugin.c"
  "$TINYALSA_DIR/src/mixer.c"
  "$TINYALSA_DIR/src/mixer_hw.c"
  "$TINYALSA_DIR/src/mixer_plugin.c"
  "$TINYALSA_DIR/src/limits.c"
  "$TINYALSA_DIR/src/snd_card_plugin.c"
)
COMMON=(-O2 -Wall -Wextra -Werror -Wno-deprecated-declarations -static
        -I"$TINYALSA_DIR/include")

"$CC_BIN" "${COMMON[@]}" -s -o "$OUT_DIR/uac2bridge" \
  "${SOURCES[@]}" -ldl -lpthread
"$CC_BIN" "${COMMON[@]}" -DBRIDGE_DIAG -o "$OUT_DIR/uac2bridge-diag" \
  "${SOURCES[@]}" -ldl -lpthread

sha256sum "$OUT_DIR/uac2bridge" "$OUT_DIR/uac2bridge-diag"
