#!/bin/zsh
# Smoke test: import the SDK App module and check every Appc name it references.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; O="$ROOT/port/appc/build"
export PYTHONHOME="$O/pyhome:$O/pyhome" PYTHONPATH="$ROOT/sdk/Build/scripts"
cd "$ROOT/sdk/Build/scripts" && "$O/python152_appc" -c "
import re, string, Appc
names = {}
for m in re.findall(r'Appc\.([A-Za-z_0-9]+)', open('App.py').read()): names[m] = 1
missing = filter(lambda n, A=Appc: not hasattr(A, n), names.keys())
print 'names referenced by App.py:', len(names), 'missing in Appc:', len(missing), missing[:10]
import App
print 'import App: OK'
"
