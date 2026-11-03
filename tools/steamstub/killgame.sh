#!/bin/bash
# killgame.sh -- kill all hl2_linux processes by exe link, never self.
me=$$
for p in $(ls /proc/ | grep -E "^[0-9]+$"); do
  [ "$p" = "$me" ] && continue
  e=$(readlink /proc/$p/exe 2>/dev/null) || continue
  case "$e" in
    *hl2_linux) kill -9 $p 2>/dev/null && echo "killed $p";;
  esac
done
echo KILL_DONE
