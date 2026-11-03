set debuginfod enabled off
set breakpoint pending on
handle SIGTRAP nostop noprint ignore
break vgui::Panel::Panel
commands
silent
bt 14
kill
quit
end
run
bt 14
