#!/usr/bin/env bash
# Headless Lua runtime smoke test for jot.
#
# Boots the real jot binary with a scratch config home whose init.lua is
# tests/runtime_smoke.lua (which exercises the whole jot.* API surface), then
# asserts:
#   - the process did not crash (a healthy run keeps running until timeout)
#   - the smoke script finished (SMOKE_DONE sentinel in its output)
#   - no pcall-wrapped call failed (FAIL=0) and no missing fields
#   - stderr contains no Lua init / callback / event-bus errors
#
# Usage: tests/run_runtime_smoke.sh [path-to-jot-binary] [timeout-seconds]
#   binary   default: build-enabled/apps/jot/jot
#   timeout  default: 8 (healthy editor keeps running; timeout kills it)
# Exit codes: 0 pass, 1 fail, 2 binary missing / unsupported environment.

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${1:-$ROOT/build-enabled/apps/jot/jot}"
TIMEOUT_SECS="${2:-8}"

if [ ! -x "$BIN" ]; then
  echo "runtime smoke: binary not found or not executable: $BIN" >&2
  echo "build it first (cmake --build build-enabled --target jot) and retry." >&2
  exit 2
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/plugins"
cp "$ROOT/tests/runtime_smoke.lua" "$WORK/init.lua"
# Lua-first config: config.lua is loaded before init.lua and must be able to
# set keys that the smoke script then reads back.
cat > "$WORK/config.lua" << 'CFG'
jot.config.set("smoke_lua_key", "from_config_lua")
jot.config.set("tab_size", 3)
CFG

echo "runtime smoke: booting $BIN with scratch config home"
JOT_CONFIG_HOME="$WORK" \
  JOT_SMOKE_OUT="$WORK/out.txt" \
  timeout "$TIMEOUT_SECS" \
  script -qec "$BIN $WORK" /dev/null >/dev/null 2>"$WORK/err.log"
STATUS=$?

# A healthy editor stays alive until the timeout kills it (124). A segfault
# (139) or any early nonzero exit is a failure.
if [ "$STATUS" -eq 139 ]; then
  echo "runtime smoke: FAIL — process segfaulted" >&2
  sed -n '1,20p' "$WORK/err.log" >&2
  exit 1
fi
if [ "$STATUS" -ne 124 ]; then
  echo "runtime smoke: FAIL — unexpected exit status $STATUS (expected 124 after timeout)" >&2
  sed -n '1,20p' "$WORK/err.log" >&2
  exit 1
fi

if [ ! -f "$WORK/out.txt" ]; then
  echo "runtime smoke: FAIL — no smoke output written (editor likely died before plugins loaded)" >&2
  sed -n '1,20p' "$WORK/err.log" >&2
  exit 1
fi

if ! grep -q "SMOKE_DONE" "$WORK/out.txt"; then
  echo "runtime smoke: FAIL — smoke script did not finish" >&2
  tail -20 "$WORK/out.txt" >&2
  exit 1
fi

# A 150ms jot.timer one-shot fires while the editor keeps running after the
# script finishes; its callback appends TIMER_FIRED to the output file.
if ! grep -q "TIMER_FIRED" "$WORK/out.txt"; then
  echo "runtime smoke: FAIL — jot.timer callback never fired in the live loop" >&2
  tail -20 "$WORK/out.txt" >&2
  exit 1
fi

FAIL_COUNT="$(grep -c '^FAIL=' "$WORK/out.txt" || true)"
FAIL_COUNT="${FAIL_COUNT#FAIL=}"
if grep -q '^FAIL=' "$WORK/out.txt"; then
  FAIL_COUNT="$(sed -n 's/^FAIL=\([0-9]*\)$/\1/p' "$WORK/out.txt" | tail -1)"
else
  FAIL_COUNT=0
fi
PASS_COUNT="$(sed -n 's/^PASS=\([0-9]*\)$/\1/p' "$WORK/out.txt" | tail -1)"
[ -n "$PASS_COUNT" ] || PASS_COUNT=0

if [ "${FAIL_COUNT:-0}" -ne 0 ]; then
  echo "runtime smoke: FAIL — $FAIL_COUNT API call(s) errored (${PASS_COUNT} passed)" >&2
  grep '=ERR:\|=MISSING:' "$WORK/out.txt" >&2
  exit 1
fi

if grep -qE "Lua API setup failed|callback error|event bus error|Plugin failed" "$WORK/err.log"; then
  echo "runtime smoke: FAIL — Lua runtime errors in stderr" >&2
  grep -E "Lua API setup failed|callback error|event bus error|Plugin failed" "$WORK/err.log" >&2
  exit 1
fi

echo "runtime smoke: PASS — ${PASS_COUNT} API checks passed, 0 failed, no crashes"
exit 0