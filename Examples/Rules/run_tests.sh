#!/bin/bash
# Run every example ruleset's tests/ scenarios through the host harness.
# Usage: ./run_tests.sh [ruleset-dir ...]   (default: all dirs with a tests/)
set -u

DIR="$(cd "$(dirname "$0")" && pwd)"

"$DIR/harness/build.sh" >/dev/null || exit 1

if [ $# -gt 0 ]; then
  dirs=("$@")
else
  dirs=()
  for d in "$DIR"/*/tests; do
    [ -d "$d" ] && dirs+=("$(dirname "$d")")
  done
fi

pass=0 failed=0
for d in "${dirs[@]}"; do
  rules="$d/rules.txt"
  if [ ! -f "$rules" ]; then
    echo "SKIP $d (no rules.txt)"
    continue
  fi
  for t in "$d"/tests/*.txt; do
    [ -f "$t" ] || continue
    rel="${t#"$DIR"/}"
    if out=$("$DIR/harness/harness" "$rules" "$t" 2>/dev/null); then
      echo "PASS $rel"
      pass=$((pass+1))
    else
      echo "FAIL $rel"
      echo "$out" | grep -E "FAIL|PARSE" | head -5 | sed 's/^/     /'
      failed=$((failed+1))
    fi
  done
done

echo "----"
echo "$pass passed, $failed failed"
[ $failed -eq 0 ]
