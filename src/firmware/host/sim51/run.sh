#!/usr/bin/env bash
# 8051 cross-check: effort.c compiled by SDCC and run in the ucsim 8051
# simulator must give the same estimates as the PC build, on the same ride.
# Also prints how long the slowest calls take. Needs sdcc and s51
# (packages sdcc, sdcc-ucsim) and gcc.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build

gcc -O1 -o build/gen gen.c -lm
(cd build && ./gen)
gcc -std=c99 -DBBSHD -Ibuild -I../fake -I../.. -o build/drive_host drive.c ../../effort.c -lm
build/drive_host > build/host.txt

F="-mmcs51 --model-large --xram-size 3840 --code-size 65536 -Ddouble=float --std-c99 -DBBSHD -Ibuild -I../.. -I../../bbsx"
sdcc $F -c -o build/effort.rel ../../effort.c
sdcc $F -c -o build/drive.rel drive.c
sdcc $F -o build/drive.ihx build/drive.rel build/effort.rel
stop=$(awk '$2 == "_sim_end" {print $1}' build/drive.map | head -1)
[ -n "$stop" ] || stop=$(grep -m1 ' _sim_end ' build/drive.map | awk '{print $2}')

# the simulator's console must stay open while it runs; it stops at sim_end()
rm -f build/out.txt
tail -f /dev/null | s51 -t 8052 -X 11.0592M -S uart=0,out=build/out.txt -e "break 0x$stop" -g build/drive.ihx > build/s51.log 2>&1 &
for _ in $(seq 1 300); do
	grep -q '^end' build/out.txt 2>/dev/null && break
	sleep 1
done
pkill -f 's51 -t 8052' || true
pkill -f 'tail -f /dev/null' || true

grep -v '^cycles' build/out.txt > build/8051.txt || true
if ! diff build/host.txt build/8051.txt; then
	echo "8051 build disagrees with the PC build (PC <, 8051 >)."
	exit 1
fi
echo "8051 and PC builds agree on $(grep -vc '^end' build/host.txt) checkpoints (learned %, valid, W, Nm x10, freewheeling, launching, launch Nm x10):"
grep -v '^end' build/host.txt | sed 's/^/  /'

# machine cycles of a classic 8051; the controller's STC15 runs one in roughly
# 1.7 of its 20 MHz clock periods
read -r _ _ edge _ process < <(grep "^cycles" build/out.txt)
awk -v e="$edge" -v p="$process" 'BEGIN {
	printf "slowest effort_add_edge(): %d cycles, about %.1f ms on the controller\n", e, e * 1.7 / 20000;
	printf "slowest effort_process():  %d cycles, about %.1f ms on the controller\n", p, p * 1.7 / 20000;
	if (e * 1.7 / 20000 > 15 || p * 1.7 / 20000 > 15) { print "too slow: the main loop (brake check) would stall"; exit 1 }
}'
