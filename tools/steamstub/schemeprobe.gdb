set debuginfod enabled off
set breakpoint pending on
handle SIGTRAP nostop noprint ignore
set $n = 0
break vgui::Panel::GetScheme
commands
silent
set $n = $n + 1
if $n > 3
continue
end
printf "SCHEME_MGR#%d=%p\n", $n, g_pVGuiSchemeManager
continue
end
run
bt 30
