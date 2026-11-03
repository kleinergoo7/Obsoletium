#!/bin/bash
# procsnap.sh -- list hl2_linux PIDs with CPU + state, no self-matching.
for p in $(ls /proc/ | grep -E "^[0-9]+$"); do
  e=$(readlink /proc/$p/exe 2>/dev/null) || continue
  case "$e" in
    *hl2_linux)
      st=$(awk '{print $3}' /proc/$p/stat 2>/dev/null)
      cpu=$(ps -o %cpu= -p $p 2>/dev/null | tr -d ' ')
      wc=$(cat /proc/$p/wchan 2>/dev/null)
      echo "PID:$p state:$st cpu%:$cpu wchan:$wc"
      ;;
  esac
done
echo SNAP_DONE
