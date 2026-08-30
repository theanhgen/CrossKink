#!/usr/bin/env bash
# Offline insurance against upstream disappearing (deleted repo, force-push, rename).
# Writes one git bundle per pinned repo into .mirror/ — gitignored, not committed.
# Restore: git clone .mirror/<name>.bundle vendor/<name>
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/.mirror"
for name in crossink simulator freeink-sdk; do
  d="$ROOT/vendor/$name"
  [ -e "$d/.git" ] || { echo "skip $name (not cloned)"; continue; }
  out="$ROOT/.mirror/$name.bundle"
  git -C "$d" bundle create "$out" --all 2>/dev/null
  printf '%-14s %s\n' "$name" "$(du -h "$out" | cut -f1)"
done
echo "bundles in .mirror/ — keep off the repo, back up with the project."
