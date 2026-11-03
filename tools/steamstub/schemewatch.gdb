set debuginfod enabled off
set breakpoint pending on
handle SIGTRAP nostop noprint ignore
set $wset = 0
break ConnectTier3Libraries
commands
silent
if $wset == 0
set $wset = 1
watch g_pVGuiSchemeManager
ignore $bpnum 1
end
continue
end
run
printf "AT_CRASH scheme=%p vtable=%p\n", g_pVGuiSchemeManager, *(void **)g_pVGuiSchemeManager
bt 25
