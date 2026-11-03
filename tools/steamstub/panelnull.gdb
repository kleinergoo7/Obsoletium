set debuginfod enabled off
set breakpoint pending on
handle SIGTRAP nostop noprint ignore
break vgui::Panel::GetScheme
commands
silent
if g_pVGuiPanel == 0
printf "PANEL_IF_NULL scheme=%p\n", g_pVGuiSchemeManager
bt 25
kill
quit
end
continue
end
run
bt 25
