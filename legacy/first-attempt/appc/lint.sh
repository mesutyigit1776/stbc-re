#!/bin/zsh
# Compile the engine sources with warnings on (the build itself is quiet): finds pointer/return type mismatches.
cd "$(dirname "$0")"; P="../python152/build"; S="$P/src"
for f in ../engine/*.c gen/appc_props.c gen/appc_props2.c; do
  clang -std=gnu99 -include gen/appc_engine.h -fsyntax-only -Wincompatible-pointer-types -Wint-conversion -Wimplicit-function-declaration -Wreturn-type -I$S/Include -I$S -I. -Igen -I../engine -I../platform $f 2>&1 | grep -E "warning|error"
done
