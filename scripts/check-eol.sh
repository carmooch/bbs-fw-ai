#!/usr/bin/env bash
# Fails if any line differs from the upstream base only by its line ending
# (CRLF <-> LF). Such changes bury real edits in diffs against upstream.
#
# Usage: scripts/check-eol.sh [base]   (default: the upstream commit this fork starts from)
set -euo pipefail

base="${1:-10cadba7745641cc655c1468c68a2b8a7894d3ed}"

full=$(git diff --numstat "$base" -- . | sort)
nocr=$(git diff --numstat --ignore-cr-at-eol "$base" -- . | sort)

if [ "$full" != "$nocr" ]; then
	echo "Line-ending-only changes found relative to $base:"
	diff <(echo "$nocr") <(echo "$full") | grep '^>' | cut -c3- || true
	echo
	echo "Restore the file's original line endings (see .gitattributes)."
	exit 1
fi

echo "No line-ending-only changes relative to $base."
