set debuginfod enabled off
set breakpoint pending on
handle SIGTRAP nostop noprint ignore
set $wset = 0
break CEngineVGui::Init
commands
silent
if $wset == 0
set $wset = 1
printf "WATCHING manager %p vtable %p\n", g_pVGuiSchemeManager, *(void **)g_pVGuiSchemeManager
watch *(void **)g_pVGuiSchemeManager
end
continue
end
run
printf "WATCHSTOP rdi=%p rdx=%d\n", $rdi, $rdx
bt 8
continue
printf "AT_CRASH scheme=%p vtable=%p\n", g_pVGuiSchemeManager, *(void **)g_pVGuiSchemeManager
bt 25
