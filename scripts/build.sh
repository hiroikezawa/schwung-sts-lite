#!/usr/bin/env bash
# Build the STS Lite module.
#   scripts/build.sh native   desktop dsp.so for the tests (build/native)
#   scripts/build.sh arm64    Move dsp.so + install tarball (dist/)
set -euo pipefail
ID=sts-lite
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
# Schwung's plugin API header ships in third_party/ (MIT, see its LICENSE),
# so the module builds on its own, outside the Schwung tree.
API="$MODULE/third_party/schwung"
case "${1:-arm64}" in
  native) CC="${CC:-gcc}"; OUT="$MODULE/build/native" ;;
  arm64) CC="${CROSS_PREFIX:-aarch64-linux-gnu-}gcc"; OUT="$MODULE/dist/$ID" ;;
  *) echo "Usage: $0 [native|arm64]" >&2; exit 2 ;;
esac
rm -rf "$OUT"
mkdir -p "$OUT"
# -fno-math-errno keeps libm calls inline on ARM (newer toolchains otherwise
# bind symbols to a glibc newer than Move's 2.34). No -ffast-math.
"$CC" -std=c11 -g -O3 -fno-math-errno -Wall -Wextra -Werror -fPIC -shared -Wl,-z,defs \
  -I"$API" -I"$MODULE/dsp" "$MODULE/dsp/sts_lite.c" "$MODULE/dsp/sts_playback_engine.c" \
  -o "$OUT/dsp.so" -lm -pthread
cp "$MODULE/module.json" "$OUT/"
if [[ "${1:-arm64}" == arm64 ]]; then
  python3 "$MODULE/scripts/check_abi.py" "$OUT/dsp.so"
  for f in ui_chain.js ui_core_v0210.mjs help.json README.md LICENSE THIRD_PARTY_LICENSES.md; do
    cp "$MODULE/$f" "$OUT/"
  done
  # Module-local fallback slots. The persistent samples live in
  # /data/UserData/UserLibrary/Samples/STS Lite/, outside the module.
  mkdir -p "$OUT/samples/left" "$OUT/samples/right"
  cp "$MODULE/samples/left/.gitkeep" "$OUT/samples/left/"
  cp "$MODULE/samples/right/.gitkeep" "$OUT/samples/right/"
  tar -czf "$MODULE/dist/$ID-module.tar.gz" -C "$MODULE/dist" "$ID"
fi
file "$OUT/dsp.so"
