#!/bin/bash
# usage: expcheck.sh <tree-dir> <ko> — is every undefined symbol of the .ko exported by generic or x86 code?
T=$1; K=$2; miss=0
for s in $(nm -u "$K" | awk '{print $2}' | sort -u); do
  re="EXPORT_[A-Z_]*SYMBOL[A-Z_]*\s*\(\s*${s}\s*[,)]"
  hit=$(cd "$T" && { git grep -lE "$re" -- ':!arch/' 2>/dev/null; git grep -lE "$re" -- 'arch/x86/' 2>/dev/null; } | head -1)
  if [ -n "$hit" ]; then printf '  %-30s exported (%s)\n' "$s" "$hit"; else printf '  %-30s ?? no literal EXPORT (generic/x86)\n' "$s"; miss=$((miss+1)); fi
done
echo "  => symbols without a literal export: $miss"
