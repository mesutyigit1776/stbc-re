#!/bin/zsh
# Build CPython 1.5.2 natively for macOS arm64 from the pristine source in third_party/.
# Output: port/python152/build/python152 (interpreter) and build/libpython1.5.a
# Changes to the original source: one patch (see patches/). Everything else is build configuration.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; HERE="$ROOT/port/python152"; B="$HERE/build"
SRC="$ROOT/third_party/Python-1.5.2"
[ -d "$SRC" ] || { echo "missing $SRC (download https://www.python.org/ftp/python/src/py152.tgz into third_party/)"; exit 1; }
rm -rf "$B"; mkdir -p "$B/bin"; cp -R "$SRC" "$B/src"; chmod -R u+w "$B/src"
# the 1.5.2 configure and sources need C89 semantics (implicit int, K&R) that current clang rejects by default
printf '#!/bin/sh\nexec clang -std=gnu89 -Wno-implicit-int -Wno-implicit-function-declaration -Wno-int-conversion -Wno-incompatible-pointer-types -Wno-return-type -Wno-deprecated-non-prototype -Wno-error -w "$@"\n' > "$B/bin/cc"
chmod +x "$B/bin/cc"; cp "$B/bin/cc" "$B/bin/gcc"
cd "$B/src"
for p in "$HERE"/patches/*.patch; do patch -p1 < "$p"; done
PATH="$B/bin:$PATH" ./configure --without-gcc > "$B/configure.log" 2>&1
# configure adds an SGI-only flag (-OPT:Olimit=0); override OPT. 'python' collides with the Python/ directory on a case-insensitive disk.
PATH="$B/bin:$PATH" make OPT=-O1 > "$B/make.log" 2>&1 || true
# second pass links the interpreter (EXE=.exe avoids the python/Python name clash; its final mv step fails harmlessly)
PATH="$B/bin:$PATH" make OPT=-O1 EXE=.exe > "$B/make2.log" 2>&1 || true
cp Modules/python "$B/python152"; cp libpython1.5.a "$B/" 2>/dev/null || true
"$B/python152" -c "import sys; print sys.version"
file "$B/python152" | cut -c1-80
