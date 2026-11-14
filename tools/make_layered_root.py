"""Make a game root in which a mod folder lies over the game folder.

Usage: python3 -I tools/make_layered_root.py GAME_DIR MOD_DIR OUT_DIR
OUT_DIR gets a folder `scripts` that holds a copy of each file of GAME_DIR/scripts. A file with the same path in
MOD_DIR/scripts replaces the copy. The copies keep the file times, so the interpreter finds the compiled files valid.
The interpreter may write compiled files into the copy. The folders GAME_DIR and MOD_DIR stay unchanged.
"""
import os
import shutil
import sys


def copy_tree(source, target):
    for folder, _, names in os.walk(source):
        relative = os.path.relpath(folder, source)
        destination = os.path.normpath(os.path.join(target, relative))
        os.makedirs(destination, exist_ok=True)
        for name in names:
            shutil.copy2(os.path.join(folder, name), os.path.join(destination, name))


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    game, mod, out = sys.argv[1:]
    shutil.rmtree(out, ignore_errors=True)
    scripts = os.path.join(out, "scripts")
    copy_tree(os.path.join(game, "scripts"), scripts)
    copy_tree(os.path.join(mod, "scripts"), scripts)
    print("layered root: %s" % out)


if __name__ == "__main__":
    main()
