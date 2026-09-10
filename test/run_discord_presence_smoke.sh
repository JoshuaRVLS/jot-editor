#!/usr/bin/env bash
# End-to-end check of Discord Rich Presence against a fake Discord IPC server.
#
# Unlike the unit tests (which drive DiscordRPC directly), this boots the real
# jot binary and lets the whole chain run: config -> editor state -> template
# rendering -> IPC. It asserts that
#   - the presence connects at all (the single-directory probe this replaces
#     never connected on installs whose socket was elsewhere),
#   - the activity Discord receives names the open file and its language key,
#   - Discord's rejection of an asset is reported back to the editor.
#
# Usage: test/run_discord_presence_smoke.sh [path-to-jot-binary] [seconds]
#   binary  default: build/apps/jot/jot
#   seconds default: 8
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${1:-$ROOT/build/apps/jot/jot}"
SECS="${2:-8}"

if [ ! -x "$BIN" ]; then
  echo "discord smoke: binary not found: $BIN" >&2
  echo "build it first (cmake --build build --target jot) and retry." >&2
  exit 2
fi

WORK="$(mktemp -d)"
FAKE_PID=""
cleanup() {
  [ -n "$FAKE_PID" ] && kill "$FAKE_PID" 2>/dev/null
  rm -rf "$WORK"
}
trap cleanup EXIT

mkdir -p "$WORK/runtime" "$WORK/home" "$WORK/project/src"
printf 'fn main() {\n    println!("hi");\n}\n' > "$WORK/project/src/main.rs"

# A socket under a scratch XDG_RUNTIME_DIR: the transport must find it there.
FRAMES="$WORK/frames.jsonl"
XDG_RUNTIME_DIR="$WORK/runtime" python3 "$ROOT/test/discord_fake_server.py" \
  "$WORK/runtime/discord-ipc-0" "$FRAMES" &
FAKE_PID=$!
for _ in $(seq 20); do
  [ -S "$WORK/runtime/discord-ipc-0" ] && break
  sleep 0.1
done
if [ ! -S "$WORK/runtime/discord-ipc-0" ]; then
  echo "discord smoke: FAIL — fake server never bound its socket" >&2
  exit 1
fi

echo "discord smoke: booting $BIN with a fake Discord on discord-ipc-0"
XDG_RUNTIME_DIR="$WORK/runtime" \
  JOT_CONFIG_HOME="$WORK/home" \
  JOT_CACHE_HOME="$WORK/home" \
  timeout "$SECS" \
  script -qec "$BIN $WORK/project/src/main.rs" /dev/null >/dev/null 2>"$WORK/err.log"
STATUS=$?
if [ "$STATUS" -ne 124 ] && [ "$STATUS" -ne 0 ]; then
  echo "discord smoke: FAIL — jot exited with $STATUS" >&2
  sed -n '1,20p' "$WORK/err.log" >&2
  exit 1
fi

if [ ! -s "$FRAMES" ]; then
  echo "discord smoke: FAIL — no IPC frames reached the fake Discord" >&2
  echo "(the transport never connected)" >&2
  sed -n '1,20p' "$WORK/err.log" >&2
  exit 1
fi

HANDSHAKE_OK=$(python3 - "$FRAMES" <<'PY'
import json, sys
frames = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]
print("yes" if any(f["opcode"] == 0 and "client_id" in f["body"] for f in frames) else "no")
PY
)
if [ "$HANDSHAKE_OK" != "yes" ]; then
  echo "discord smoke: FAIL — no handshake frame" >&2
  cat "$FRAMES" >&2
  exit 1
fi

python3 - "$FRAMES" <<'PY'
import json, sys
frames = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]
activity = None
for frame in frames:
    if frame["opcode"] == 1 and "SET_ACTIVITY" in frame["body"]:
        activity = json.loads(frame["body"])["args"].get("activity", {})
        if activity:
            break
if not activity:
    print("discord smoke: FAIL — no activity was ever set", file=sys.stderr)
    sys.exit(1)
details = activity.get("details", "")
assets = activity.get("assets", {})
large = assets.get("large_image", "")
small = assets.get("small_image", "")
problems = []
if "main.rs" not in details:
    problems.append(f"details do not name the open file: {details!r}")
if large != "rust":
    problems.append(f"large image is {large!r}, expected 'rust'")
if small != "jot":
    problems.append(f"small image is {small!r}, expected 'jot'")
if problems:
    print("discord smoke: FAIL — " + "; ".join(problems), file=sys.stderr)
    print(json.dumps(activity, indent=2), file=sys.stderr)
    sys.exit(1)
print(f"discord smoke: PASS — activity sent: {details!r} [{large}]")
PY
