set debuginfod enabled off
set breakpoint pending on
handle SIGTRAP nostop noprint ignore
run
bt 30
