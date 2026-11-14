#!/usr/bin/env python3
"""Build a merged scripts root for a mod run.

Usage: port/appc/make_mod_root.py NAME LAYER [LAYER ...]
  LAYER is a game-style folder that has a scripts/ folder, lowest priority first (the game folder, then the mod's folder).
  The result is port/appc/roots/root_NAME/scripts/: every file is a symlink to the layer that gives it. A module of a higher layer
  replaces the lower layer's module of the same name (its .py and its .pyc), so the mod's files win even when the two layers ship
  different file kinds for one module.

Run a mod with the merged root and the data roots (the highest priority first; the game folder last):
  APPC_ROOT=<repo>/port/appc/roots/root_NAME APPC_DATA_ROOTS=<orion>:<remastered>:<game> port/appc/run.sh ../port/appc/host_quickbattle.py
"""
import os, sys, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_BASE = os.path.join(HERE, 'roots')

def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    name, layers = argv[1], [os.path.abspath(p) for p in argv[2:]]
    out = os.path.join(OUT_BASE, 'root_' + name, 'scripts')
    if os.path.isdir(os.path.dirname(out)):
        shutil.rmtree(os.path.dirname(out))
    os.makedirs(out)
    modules = {}                                  # module stem (path without .py / .pyc) -> the linked file
    count = 0
    for layer in layers:
        src = os.path.join(layer, 'scripts')
        if not os.path.isdir(src):
            print('no scripts folder in', layer)
            return 1
        for d, dirs, files in os.walk(src):
            for f in files:
                full = os.path.join(d, f)
                rel = os.path.relpath(full, src)
                stem, ext = os.path.splitext(rel)
                if ext in ('.py', '.pyc'):
                    if stem in modules:
                        old = os.path.join(out, modules[stem])
                        if os.path.lexists(old):
                            os.remove(old)
                    modules[stem] = rel
                dst = os.path.join(out, rel)
                if os.path.lexists(dst):
                    os.remove(dst)
                if not os.path.isdir(os.path.dirname(dst)):
                    os.makedirs(os.path.dirname(dst))
                os.symlink(full, dst)
                count += 1
    fixes = os.path.join(HERE, 'mod_fixes', 'scripts')
    for d, dirs, files in os.walk(fixes) if os.path.isdir(fixes) else []:
        for f in files:
            if not f.endswith('.py'):
                continue
            rel = os.path.relpath(os.path.join(d, f), fixes)
            stem = os.path.splitext(rel)[0]
            for ext in ('.pyc', '.py'):
                old = os.path.join(out, stem + ext)
                if os.path.lexists(old):
                    if ext == '.pyc':
                        target = os.readlink(old) if os.path.islink(old) else old
                        os.symlink(target, os.path.join(out, stem + '_base.pyc'))
                    os.remove(old)
            shutil.copyfile(os.path.join(fixes, rel), os.path.join(out, rel))
            print('fix', rel)
    print('root', out, 'links', count, 'modules', len(modules))
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
