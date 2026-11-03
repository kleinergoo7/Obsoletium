set debuginfod enabled off
set breakpoint pending on
handle SIGTRAP nostop noprint ignore
break se::vulkan::SeVkShaderPack_Create
commands
silent
printf "PACK_CREATE isVertex=%d name=%s\n", $r8, $r9
continue
end
run
bt
