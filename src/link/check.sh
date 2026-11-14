#!/usr/bin/env bash
# Checks bbs-link against a virtual controller (the ride simulator running
# the real firmware protocol code on a pseudo-terminal): live data, assist
# level, every settings safety rule, undo, the pedal sensor capture, and that
# stopping leaves the level at 0. Linux, needs dotnet 8 and gcc.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
host="$here/../firmware/host"
work=$(mktemp -d)
make -s -C "$host" ride_sim
dotnet build -c Release "$here/bbs-link.csproj" -o "$work/bin" -v q -nologo > "$work/build.log" 2>&1 || { cat "$work/build.log"; exit 1; }
link="$work/bin/bbs-link"

"$host/ride_sim" "$host/sim/live.sim" /dev/null --serial > "$work/vc.out" 2>&1 &
vc=$!
cleanup() { "$link" stop > /dev/null 2>&1 || true; sleep 1; kill $vc 2> /dev/null || true; rm -rf "$work"; }
trap cleanup EXIT
for _ in $(seq 50); do grep -q 'serial:' "$work/vc.out" && break; sleep 0.1; done
pty=$(sed -n 's/^serial: //p' "$work/vc.out")

"$link" serve --port "$pty" --log-dir "$work/live" > "$work/serve.out" 2>&1 &
failures=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; failures=$((failures + 1)); }
check() { if eval "$2"; then pass "$1"; else fail "$1"; fi; }
json() { "$link" "$@" --json 2> /dev/null || true; }
# refused WHY CMD...: the command fails, saying WHY
refused() { local why=$1; shift; local out; if out=$("$link" "$@" 2>&1); then return 1; fi; echo "$out" | grep -q "$why"; }
field() { python3 -c 'import json,sys; d=json.load(sys.stdin); print(eval(sys.argv[1]))' "$1"; }

for _ in $(seq 60); do [ "$(json status | field 'd.get("connected")')" = "True" ] && break; sleep 0.5; done
check "connects" '[ "$(json status | field "d[\"connected\"]")" = "True" ]'
sleep 1
check "sets level 0 on connecting" '[ "$(json status | field "d[\"live\"][\"level\"]")" = "0" ]'
check "a second server is refused" '! timeout 10 "$link" serve --port "$pty" --log-dir "$work/live2" > /dev/null 2>&1'

sleep 3
"$link" level 3 > /dev/null
check "level 3 gives assist while pedalling" '[ "$(json watch 2 | field "max(s[\"current_pct\"] for s in d[\"samples\"])")" -gt 0 ]'
check "settings refused while pedalling" 'refused "the cranks are turning" config set LaunchSensing=true'
check "level out of range refused" 'refused "Level must be 0-9" level 10'

cap=$(json capture)
check "pedal sensor capture" '[ "$(echo "$cap" | field "d[\"usable\"]")" = "True" ]'

# wait until the cranks have stopped (25 s in) and the wheel too
for _ in $(seq 120); do
	s=$(json status)
	[ "$(echo "$s" | field 'd["live"]["cadence_rpm"] == 0 and not d["live"]["wheel_moving"]')" = "True" ] && break
	sleep 0.5
done
check "max current over the cap refused" 'refused "over this link" config set MaxCurrentAmps=35'
check "battery voltages need --battery" 'refused "add --battery" config set LowCutoffVolts=40'
check "config tool limits apply" 'refused "must be in interval" config set "StandardAssistLevels[3].MaxCurrentPercent=150"'
check "unknown setting refused" 'refused "No setting called Bogus" config set Bogus=1'
"$link" config set LaunchSensing=true "StandardAssistLevels[3].MaxCurrentPercent=20" --dry-run > /dev/null
check "dry run writes nothing" '[ "$(json config get LaunchSensing | field "d[\"settings\"][\"LaunchSensing\"]")" = "false" ]'
check "set" '"$link" config set LaunchSensing=true "StandardAssistLevels[3].MaxCurrentPercent=20" > /dev/null'
check "set took" '[ "$(json config get LaunchSensing "StandardAssistLevels[3].MaxCurrentPercent" | field "list(d[\"settings\"].values())")" = "['"'"'true'"'"', '"'"'20'"'"']" ]'
check "undo" '"$link" config undo > /dev/null'
check "undo took" '[ "$(json config get LaunchSensing "StandardAssistLevels[3].MaxCurrentPercent" | field "list(d[\"settings\"].values())")" = "['"'"'false'"'"', '"'"'14'"'"']" ]'
check "backups kept" '[ "$(ls "$work/live/backups" | wc -l)" -ge 2 ]'
"$link" mark "test mark" > /dev/null
check "dashboard page served" 'curl -fs http://localhost:8710/ | grep -q "BBSHD Live"'
check "dashboard has live readings" '[ "$(curl -fs "http://localhost:8710/data?since=0" | field "len(d[\"samples\"]) > 20 and d[\"connected\"]")" = "True" ]'
check "dashboard shows what Claude did" 'curl -fs "http://localhost:8710/data?since=0" | grep -q "note: test mark"'
check "dashboard is read-only" '[ "$(curl -s -o /dev/null -w "%{http_code}" -X POST -d level=9 http://localhost:8710/data)" = "405" ]'
sleep 1
check "marks reach the log" 'grep -q "test mark" "$work"/live/live-*.csv'
check "live data logged" '[ "$(wc -l < "$(ls "$work"/live/live-*.csv)")" -gt 20 ]'

# a USB-serial adapter that wedges (writes block, nothing comes back) while the
# port still looks open: commands get an error instead of hanging, the link
# says it's lost and to replug, and it reopens the port by itself at level 0
"$link" test-wedge > /dev/null
t0=$(date +%s)
rc=0; timeout 60 "$link" level 0 > "$work/wedge.out" 2>&1 || rc=$?
check "a command on a wedged port fails instead of hanging" '[ $rc -ne 0 ] && [ $rc -ne 124 ] && [ $(( $(date +%s) - t0 )) -lt 30 ]'
check "it says the controller was lost" 'grep -qi "lost the controller" "$work/wedge.out"'
check "the stall is reported, saying to replug" 'grep -q "lost the controller: .*replug the USB end" "$work"/live/events-*.log'
for _ in $(seq 120); do [ "$(json status | field 'd.get("connected")')" = "True" ] && break; sleep 0.5; done
sleep 2
check "reconnects after the stall" '[ "$(json status | field "d[\"connected\"] and d[\"age_ms\"] < 2000")" = "True" ]'
check "level 0 after reconnecting" '[ "$(json status | field "d[\"live\"][\"level\"]")" = "0" ]'
check "commands work again" '[ "$(json config get LaunchSensing | field "d[\"settings\"][\"LaunchSensing\"]")" = "false" ]'

"$link" stop > /dev/null
sleep 2
check "stop sets level 0" 'grep -q "stopping: level set to 0" "$work"/live/events-*.log'
check "server gone after stop" '! "$link" status > /dev/null 2>&1'
check "dashboard gone after stop" '! curl -fs http://localhost:8710/ > /dev/null'

echo
if [ $failures -eq 0 ]; then echo "bbs-link checks passed."; else echo "$failures bbs-link check(s) failed."; fi
[ $failures -eq 0 ]
