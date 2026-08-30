#!/usr/bin/env bash
# Prepare the Kindle cross-build in this fork.
#
# This repo IS CrossInk, so there is nothing to clone for it. Two submodules
# are pulled and pinned, and the simulator patch is applied on top. Idempotent,
# and loud when it is not.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
LOCK="$ROOT/UPSTREAM.lock"

die() { echo "!! $*" >&2; exit 1; }

[ -f "$LOCK" ] || die "no $LOCK"
# The lock is TAB-separated. An editor that expands tabs silently breaks every
# lookup, so fail loudly rather than reporting 'not in lock' for everything.
grep -q "$(printf '\t')" "$LOCK" \
  || die "$LOCK has no TAB characters — it must be tab-separated, not spaces"

field() { awk -F'\t' -v k="$1" -v n="$2" '$1==k {print $n}' "$LOCK"; }

echo "==> initialising submodules"
git submodule update --init --recursive freeink-sdk vendor/simulator

# Verify the pins match UPSTREAM.lock. freeink-sdk is CrossInk's own submodule,
# so a rebase onto newer upstream can move it without us noticing.
for pair in "freeink-sdk:freeink-sdk" "simulator:vendor/simulator"; do
  name="${pair%%:*}"; path="${pair##*:}"
  want="$(field "$name" 3)"
  [ -n "$want" ] || die "$name has no SHA in $LOCK"
  have="$(git -C "$path" rev-parse HEAD)"
  if [ "$want" != "$have" ]; then
    echo "==> $path: pinning to ${want:0:8} (was ${have:0:8})"
    git -C "$path" fetch --quiet --tags origin || die "fetch failed for $name"
    git -C "$path" checkout --quiet --force "$want" \
      || die "$name: $want not reachable (force-pushed or deleted?)"
  else
    echo "==> $path -> ${have:0:8} OK"
  fi
done

# Patches against pinned upstream. Idempotent: skip cleanly if already applied,
# fail loudly if the patch no longer matches (which is what an upstream bump
# looks like). CrossInk's own Kindle changes are commits in this fork, not
# patches — only the simulator still needs one.
shopt -s nullglob
for p in "$ROOT"/patches/*.patch; do
  base="$(basename "$p")"
  repo="${base%%.*}"
  case "$repo" in
    simulator) dir="$ROOT/vendor/simulator" ;;
    *) die "$base targets unknown repo '$repo'" ;;
  esac
  [ -d "$dir" ] || die "$base targets $dir which does not exist"
  if git -C "$dir" apply --reverse --check "$p" 2>/dev/null; then
    echo "==> $base already applied"
  elif git -C "$dir" apply --check "$p" 2>/dev/null; then
    echo "==> applying $base"
    git -C "$dir" apply "$p"
  else
    die "$base no longer applies to $dir at its pinned SHA.
    Upstream moved under the patch. Regenerate it, or pin $repo back."
  fi
done

echo
echo "OK. Upstream is read-only — never push to uxjulia/* or Free-Ink/*."
