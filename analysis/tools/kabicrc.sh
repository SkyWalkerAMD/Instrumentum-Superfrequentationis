#!/bin/bash
# usage: kabicrc.sh <repo> <ko> <branch...>  — CRC of each import in each branch's kABI stablelist ("-" = not stablelisted)
R=$1; K=$2; shift 2
syms=$(nm -u "$K" | awk '{print $2}' | sort -u)
printf '%-28s' symbol; for b in "$@"; do printf '%-12s' ${b#rocky}; done; echo
changed=0
for s in $syms; do
  line=$(printf '%-28s' "$s"); vals=""
  for b in "$@"; do
    if [ "$b" = HEAD ]; then c=$(cd $R && cat redhat/kabi/kabi-module/kabi_x86_64/$s 2>/dev/null | awk '/^0x/{print $1}' | tail -1)
    else c=$(cd $R && timeout 60 git show origin/$b:redhat/kabi/kabi-module/kabi_x86_64/$s 2>/dev/null | awk '/^0x/{print $1}' | tail -1); fi
    [ -z "$c" ] && c="-"; line="$line$(printf '%-12s' $c)"; vals="$vals $c"
  done
  n=$(echo $vals | tr ' ' '\n' | grep -v '^-$' | sort -u | wc -l)
  [ "$n" -gt 1 ] && { line="$line  <-- CRC changes"; changed=$((changed+1)); }
  echo "$line"
done
echo "symbols whose kABI CRC differs between these minors: $changed / $(echo $syms | wc -w)"
