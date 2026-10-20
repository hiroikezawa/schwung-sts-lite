#!/usr/bin/env bash
# Native build + every test: engine (ASan/UBSan), UI, and the Move build's ABI.
set -euo pipefail
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
bash "$MODULE/scripts/build.sh" native
OUT="$MODULE/build/native"
"${CC:-gcc}" -std=c11 -O1 -g -Wall -Wextra -Werror -fno-math-errno \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  -I"$MODULE/third_party/schwung" -I"$MODULE/dsp" \
  "$MODULE/tests/test_engine.c" "$MODULE/dsp/sts_playback_engine.c" \
  -o "$OUT/test-engine" -lm -pthread
(cd "$MODULE" && "$OUT/test-engine")
# The UI test drives the real Schwung page controller: SCHWUNG_ROOT, or a
# sibling ../schwung checkout.
SCHWUNG="${SCHWUNG_ROOT:-$MODULE/../schwung}"
if [[ ! -f "$SCHWUNG/src/shared/param_pages/page_controller.mjs" ]]; then
  echo "SKIP: UI test needs a Schwung checkout (set SCHWUNG_ROOT)" >&2
elif command -v node >/dev/null 2>&1; then
  SCHWUNG_ROOT="$SCHWUNG" node "$MODULE/tests/test_ui.mjs"
elif command -v node.exe >/dev/null 2>&1 && command -v wslpath >/dev/null 2>&1; then
  SCHWUNG_ROOT="$(wslpath -w "$SCHWUNG")" WSLENV=SCHWUNG_ROOT \
    node.exe "$(wslpath -w "$MODULE/tests/test_ui.mjs")"
else
  echo "Node.js is required for the UI test" >&2; exit 1
fi
if command -v "${CROSS_PREFIX:-aarch64-linux-gnu-}gcc" >/dev/null 2>&1; then
  bash "$MODULE/scripts/build.sh" arm64
else
  echo "SKIP: no aarch64 cross compiler; ABI not checked" >&2
fi
