#!/usr/bin/env bash
# Fails if any line differs from the upstream base only by its line ending
# (CRLF <-> LF). Such changes bury real edits in diffs against upstream.
#
# Lines that are unchanged ignoring CR are paired up through
# `git diff --ignore-cr-at-eol` and their endings compared one by one.
#
# Usage: scripts/check-eol.sh [base]   (default: the upstream commit this fork starts from)
set -euo pipefail

base="${1:-10cadba7745641cc655c1468c68a2b8a7894d3ed}"

python3 - "$base" <<'PY'
import re, subprocess, sys

base = sys.argv[1]

def git(*args):
	return subprocess.run(["git", *args], check=True, capture_output=True).stdout

files = git("diff", "--name-only", "--diff-filter=M", base, "--", ".").decode().split("\n")
bad = []
for path in filter(None, files):
	old = git("show", f"{base}:{path}").split(b"\n")
	with open(path, "rb") as f:
		new = f.read().split(b"\n")
	hunks = []
	for line in git("diff", "-U0", "--ignore-cr-at-eol", base, "--", path).split(b"\n"):
		m = re.match(rb"@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@", line)
		if m:
			a, b, c, d = m.groups()
			b = 1 if b is None else int(b)
			d = 1 if d is None else int(d)
			# 0-based start of each side; a zero-length side is numbered
			# from the line before it
			hunks.append((int(a) - (b > 0), b, int(c) - (d > 0), d))
	i = j = 0	# 0-based indices of the next unpaired old/new lines
	for a, b, c, d in hunks + [(len(old), 0, len(new), 0)]:
		while i < a and j < c:
			if old[i].endswith(b"\r") != new[j].endswith(b"\r"):
				bad.append(f"{path}:{j + 1}")
			i += 1
			j += 1
		i, j = a + b, c + d

if bad:
	print(f"Line-ending-only changes found relative to {base}:")
	print("\n".join(bad))
	print()
	print("Restore the file's original line endings (see .gitattributes).")
	sys.exit(1)

print(f"No line-ending-only changes relative to {base}.")
PY
