import sys, strop

start = sys.path
prog = bc_test.progpath()
check('start path has three entries ' + repr(start), len(start) == 3)
check('start path begins with .\\Scripts and .', start[:2] == ['.\\Scripts', '.'])
check('start path ends with the program folder', prog[:3] == 'C:\\' and start[2] == prog[:strop.rfind(prog, '\\')])

import plain
check('module of .\\Scripts imports', plain.marker_plain == 'plain')
check('file name has the Windows form', plain.__file__ == '.\\Scripts\\plain.py')
import rootmod
check('module of . imports', rootmod.marker_root == 'root' and rootmod.__file__ == '.\\rootmod.py')
check('the compiled file is written next to the source', len(open('Scripts\\plain.pyc', 'rb').read()) > 8)

try:
    __import__('PLAIN2')
    check('missing module', 0)
except ImportError:
    check('missing module', 1)

try:
    __import__('Rootmod')
    check('wrong case is refused', 0)
except NameError, msg:
    check('wrong case is refused', strop.find(msg, 'Case mismatch for module name Rootmod') == 0)

check('open() maps a Windows path', open('Scripts\\plain.py').read() == "marker_plain = 'plain'\n")
check('open() ignores case', open('SCRIPTS/PLAIN.PY').read() == "marker_plain = 'plain'\n")
