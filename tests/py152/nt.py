import nt, strop

def raises(exc, func, *args):
    try:
        apply(func, args)
    except exc, value:
        return value
    return None

def sorted(names):
    names.sort()
    return names

# Module content (E0110)
kept = ['access', 'listdir', 'lstat', 'mkdir', 'rename', 'rmdir', 'stat', 'unlink', 'remove', 'utime',
        'times', 'open', 'close', 'dup', 'dup2', 'lseek', 'read', 'write', 'fstat', 'fdopen']
removed = ['chdir', 'chmod', 'umask', '_exit', 'getpid', 'getcwd', 'system', 'popen', 'execv', 'execve',
           'spawnv', 'spawnve', 'pipe', 'putenv', 'strerror']
check('the 20 functions exist', len(filter(lambda n: hasattr(nt, n), kept)) == 20)
check('the 15 removed functions are absent', len(filter(lambda n: hasattr(nt, n), removed)) == 0)
check('module name', nt.__name__ == 'nt')
check('error is OSError', nt.error is OSError)
check('environ is a dictionary', type(nt.environ) == type({}))

# Constants with the values of Windows (E0110)
values = [('F_OK', 0), ('R_OK', 4), ('W_OK', 2), ('X_OK', 1), ('O_RDONLY', 0), ('O_WRONLY', 1), ('O_RDWR', 2),
          ('O_APPEND', 8), ('O_CREAT', 0x100), ('O_EXCL', 0x400), ('O_TRUNC', 0x200), ('O_BINARY', 0x8000),
          ('O_TEXT', 0x4000), ('P_WAIT', 0), ('P_NOWAIT', 1), ('P_OVERLAY', 2), ('P_NOWAITO', 3), ('P_DETACH', 4)]
bad = []
for name, value in values:
    if not hasattr(nt, name) or getattr(nt, name) != value:
        bad.append(name)
check('constants have the Windows values ' + repr(bad), bad == [])
check('no other O_ constants', not hasattr(nt, 'O_NONBLOCK') and not hasattr(nt, 'O_NDELAY') and not hasattr(nt, 'WNOHANG'))

# Name mapping of the host layer
check('relative path with backslashes', bc_test.host_path('Custom\\Hello.txt')[-17:] == '/Custom/Hello.txt')
check('wrong case maps to the case of the file system', bc_test.host_path('cUSTOM\\hello.TXT')[-17:] == '/Custom/Hello.txt')
check('slashes work as separators', bc_test.host_path('custom/mixed.txt')[-17:] == '/Custom/MiXeD.TxT')
check('dot and dot-dot are removed', bc_test.host_path('Custom\\Sub\\..\\.\\Hello.txt')[-17:] == '/Custom/Hello.txt')
check('a missing name keeps its case', bc_test.host_path('Custom\\NewFile.TXT')[-19:] == '/Custom/NewFile.TXT')
check('the current folder is a Windows path', bc_test.cwd()[:3] == 'C:\\' and strop.find(bc_test.cwd(), '/') < 0)
check('full path joins the current folder', bc_test.full_path('x\\y')[:len(bc_test.cwd())] == bc_test.cwd())

# Path checks (E0111)
check('access: Custom file', nt.access('Custom\\Hello.txt', nt.F_OK) == 1)
check('access: wrong case', nt.access('CUSTOM\\hello.txt', nt.F_OK) == 1)
check('access: slashes', nt.access('Custom/Hello.txt', nt.F_OK) == 1)
check('access: scripts Custom file', nt.access('scripts\\Custom\\Mod.txt', nt.F_OK) == 1)
check('access: scripts Custom, wrong case', nt.access('SCRIPTS\\custom\\MOD.TXT', nt.F_OK) == 1)
check('access: missing file in Custom', nt.access('Custom\\Missing.txt', nt.F_OK) == 0)
check('access: file outside Custom', nt.access('other\\file.txt', nt.F_OK) == 0)
check('access: the Custom folder itself', nt.access('Custom\\', nt.F_OK) == 0 and nt.access('Custom', nt.F_OK) == 0)
check('access: near miss CustomX', nt.access('CustomX\\file.txt', nt.F_OK) == 0)
check('access: dot-dot leaves Custom', nt.access('Custom\\..\\other\\file.txt', nt.F_OK) == 0)
check('access: dot-dot stays in Custom', nt.access('Custom\\Sub\\..\\Hello.txt', nt.F_OK) == 1)
check('access: a path under cwd-less drive path', nt.access('C:\\nowhere\\Custom\\Hello.txt', nt.F_OK) == 0)

# listdir (E0111, E0112, B0011)
check('listdir Custom', sorted(nt.listdir('Custom')) == ['Hello.txt', 'MiXeD.TxT', 'Sub'])
check('listdir keeps the names of the file system', 'MiXeD.TxT' in nt.listdir('custom'))
check('listdir with a trailing separator', sorted(nt.listdir('Custom\\Sub\\')) == ['inner.txt'])
check('listdir scripts Custom', nt.listdir('SCRIPTS/custom') == ['Mod.txt'])
check('listdir CustomX passes the folder check (B0011)', nt.listdir('CustomX') == ['file.txt'])
check('listdir outside: ValueError not allowed', raises(ValueError, nt.listdir, 'other') == 'not allowed')
check('listdir root: ValueError not allowed', raises(ValueError, nt.listdir, '.') == 'not allowed')
check('listdir long path: ValueError path too long', raises(ValueError, nt.listdir, 'Custom\\' + 'x' * 300) == 'path too long')
check('listdir limit 0x103', raises(ValueError, nt.listdir, 'x' * 0x104) == 'path too long' and raises(ValueError, nt.listdir, 'x' * 0x103) == 'not allowed')
value = raises(OSError, nt.listdir, 'Custom\\Nope')
check('listdir missing folder: OSError 3', value is not None and value[0] == 3)

# stat, lstat (E0111, E0112)
check('stat size', nt.stat('Custom\\Hello.txt')[6] == 6)
check('stat wrong case', nt.stat('CUSTOM\\HELLO.TXT')[6] == 6)
check('lstat is stat', nt.lstat('Custom\\Hello.txt')[6:] == nt.stat('Custom\\Hello.txt')[6:])
check('stat of a folder has the directory bit', nt.stat('Custom\\Sub\\inner.txt')[0] & 0170000 == 0100000)
value = raises(OSError, nt.stat, 'other\\file.txt')
check('stat outside: OSError with the file name', value is not None and len(value) == 3 and value[2] == 'other\\file.txt')
value = raises(OSError, nt.lstat, 'other\\file.txt')
check('lstat outside: OSError with the file name', value is not None and value[2] == 'other\\file.txt')
value = raises(OSError, nt.stat, 'Custom\\Missing.txt')
check('stat missing file: OSError with the file name', value is not None and value[2] == 'Custom\\Missing.txt' and value[0] == 2)

# open, write, read, lseek, fstat, dup, dup2, close, fdopen
fd = nt.open('custom\\IO.bin', nt.O_RDWR | nt.O_CREAT | nt.O_TRUNC | nt.O_BINARY)
check('open returns a descriptor', type(fd) == type(0) and fd > 2)
check('write', nt.write(fd, 'abcdef') == 6)
check('lseek', nt.lseek(fd, 1, 0) == 1)
check('read', nt.read(fd, 3) == 'bcd')
check('fstat size', nt.fstat(fd)[6] == 6)
fd2 = nt.dup(fd)
check('dup', fd2 != fd and nt.lseek(fd2, 0, 0) == 0)
check('dup shares the position', nt.read(fd, 2) == 'ab')
fd3 = nt.open('custom\\IO2.bin', nt.O_WRONLY | nt.O_CREAT | nt.O_BINARY)
nt.dup2(fd, fd3)
check('dup2', nt.read(fd3, 2) == 'cd')
nt.close(fd3)
nt.close(fd2)
nt.close(fd)
check('close', raises(OSError, nt.close, fd) is not None)
check('the file exists with the case that was given', 'IO.bin' in nt.listdir('Custom'))
fd = nt.open('Custom\\IO.bin', nt.O_RDONLY)
f = nt.fdopen(fd, 'rb')
check('fdopen', f.read() == 'abcdef')
f.close()
value = raises(OSError, nt.open, 'other\\file.txt', nt.O_RDONLY)
check('open outside: OSError with the file name', value is not None and value[2] == 'other\\file.txt')
value = raises(OSError, nt.open, 'Custom\\Missing.txt', nt.O_RDONLY)
check('open missing file: OSError', value is not None and value[0] == 2)
fd = nt.open('Custom\\Excl.bin', nt.O_WRONLY | nt.O_CREAT | nt.O_EXCL)
nt.close(fd)
check('O_EXCL refuses an existing file', raises(OSError, nt.open, 'Custom\\Excl.bin', nt.O_WRONLY | nt.O_CREAT | nt.O_EXCL) is not None)
fd = nt.open('Custom\\IO2.bin', nt.O_WRONLY | nt.O_APPEND)
nt.write(fd, 'xy')
nt.close(fd)
check('O_APPEND', nt.stat('Custom\\IO2.bin')[6] == 2)

# mkdir, rmdir, rename, unlink, remove, utime (E0111, E0112, B0010)
check('mkdir returns None', nt.mkdir('Custom\\NewDir') is None)
check('mkdir made the folder', nt.stat('CUSTOM\\NEWDIR')[0] & 0170000 == 0040000)
check('mkdir existing folder: OSError', raises(OSError, nt.mkdir, 'Custom\\NewDir') is not None)
check('rename returns None', nt.rename('custom\\newdir', 'Custom\\Renamed') is None)
check('rename result', 'Renamed' in nt.listdir('Custom') and 'NewDir' not in nt.listdir('Custom'))
check('rmdir', nt.rmdir('CUSTOM\\RENAMED') is None and 'Renamed' not in nt.listdir('Custom'))
nt.utime('Custom\\Hello.txt', (1000000, 2000000))
check('utime', nt.stat('Custom\\Hello.txt')[7] == 1000000 and nt.stat('Custom\\Hello.txt')[8] == 2000000)
check('unlink', nt.unlink('custom\\io.bin') is None and 'IO.bin' not in nt.listdir('Custom'))
check('remove', nt.remove('Custom\\IO2.bin') is None and 'IO2.bin' not in nt.listdir('Custom'))
check('remove missing file: OSError', raises(OSError, nt.remove, 'Custom\\IO2.bin') is not None)
value = raises(OSError, nt.unlink, 'other\\file.txt')
check('unlink outside: OSError with the file name', value is not None and value[2] == 'other\\file.txt')
value = raises(OSError, nt.remove, 'other\\file.txt')
check('remove outside: OSError with the file name', value is not None and value[2] == 'other\\file.txt')
value = raises(OSError, nt.rmdir, 'other')
check('rmdir outside: OSError with the file name', value is not None and value[2] == 'other')
value = raises(OSError, nt.utime, 'other\\file.txt', (1, 2))
check('utime outside: OSError with the file name', value is not None and value[2] == 'other\\file.txt')
check('other\\file.txt is untouched', open('other/file.txt').read() == 'other file\n')

# B0010: mkdir and rename return NULL with no exception set
check('mkdir outside returns NULL with no exception (B0010)', bc_test.call_raw(nt.mkdir, ('other\\newdir',)) == (1, 0))
check('rename from outside returns NULL with no exception (B0010)', bc_test.call_raw(nt.rename, ('other\\file.txt', 'Custom\\x.txt')) == (1, 0))
check('rename to outside returns NULL with no exception (B0010)', bc_test.call_raw(nt.rename, ('Custom\\Hello.txt', 'other\\x.txt')) == (1, 0))
check('mkdir outside made nothing', nt.access('other\\newdir', 0) == 0 and open('other/file.txt').read() == 'other file\n')
check('mkdir inside raises normally', bc_test.call_raw(nt.mkdir, ('Custom\\Sub',)) == (1, 1))
check('the call machinery turns the NULL into SystemError', raises(SystemError, nt.mkdir, 'other\\newdir') == 'NULL result without error in call_object')

# times (E0110)
t = nt.times()
check('times has five floats, the last three are 0', len(t) == 5 and t[2] == 0.0 and t[3] == 0.0 and t[4] == 0.0)
check('times: user time is not negative', t[0] >= 0.0 and t[1] >= 0.0)
