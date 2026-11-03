set debuginfod enabled off
set breakpoint pending on
handle SIGTRAP nostop noprint ignore
break CMatRenderContextBase::Bind
ignore 1 5
commands
silent
bt 25
kill
quit
end
run
bt
