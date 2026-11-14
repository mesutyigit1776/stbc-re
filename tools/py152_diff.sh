#!/bin/sh
# Usage: tools/py152_diff.sh path/to/py152.tgz
# Prints one unified diff for each changed file and one line for each file that only the repository has.
set -eu
TGZ="$1"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
tar xzf "$TGZ" -C "$TMP"
cd "$ROOT/third_party/Python-1.5.2"
find . -type f | sort | while read -r f; do
  if [ ! -f "$TMP/Python-1.5.2/$f" ]; then echo "Only in repo: $f"; else
    diff -u "$TMP/Python-1.5.2/$f" "$f" || true
  fi
done
