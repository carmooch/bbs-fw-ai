#!/usr/bin/env bash
# Builds the model, learns the magnet pattern from 5 minutes of high-gear
# cruising (gear 3.5, 90 rpm, 20 Nm), then prints the accuracy map.
set -euo pipefail
cd "$(dirname "$0")"
gcc -O2 -o ripple_model ripple_model.c -lm
./ripple_model 3.5 90 20 0.8 0.8 0.3 10 0.056 3 42 300 96 100 0.1 > learn_raw.txt
python3 -I -c '
rows=[list(map(float,l.split())) for l in open("learn_raw.txt") if l.strip()]
avg=[sum(r[i] for r in rows)/len(rows) for i in range(len(rows[0]))]
open("learned.txt","w").write(" ".join(f"{a:.8f}" for a in avg))'
python3 -I final.py
