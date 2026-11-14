# Windows os module alias for scripts that import nt directly (the original ran on Windows).
# Paths in the scripts use backslashes ('scripts\\Custom\\Ships'); translate them for posix.
import posix
import string
from posix import *
error = posix.error

def _p(path):
    return string.replace(path, '\\', '/')

def listdir(path):
    return posix.listdir(_p(path))

def stat(path):
    return posix.stat(_p(path))

def remove(path):
    return posix.remove(_p(path))

def unlink(path):
    return posix.unlink(_p(path))

def mkdir(path, mode=0777):
    return posix.mkdir(_p(path), mode)

def rmdir(path):
    return posix.rmdir(_p(path))

def rename(a, b):
    return posix.rename(_p(a), _p(b))

def chdir(path):
    return posix.chdir(_p(path))

def open(path, flags, mode=0777):
    return posix.open(_p(path), flags, mode)
