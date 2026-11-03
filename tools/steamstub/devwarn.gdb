set debuginfod enabled off
set breakpoint pending on
handle SIGTRAP nostop noprint ignore
break DevWarning
commands
silent
printf "DEVWARN: "
x/s $rsi
continue
end
run
bt
