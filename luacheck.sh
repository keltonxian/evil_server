#!/bin/bash

if [ -z $1 ]; then
	export filename="logic.lua"
else
	export filename=$1
fi

echo Checking: $filename

## lua_simplifier logic.lua > simp.lua
lua_simplifier $filename > simp.lua

lua_checker -no_reuse_varnames -const_functions simp.lua

