import sys, new, strop

expected = ['array', 'binascii', 'cmath', 'errno', 'math', 'new', 'nt', 'operator', 'regex', 'strop',
            'struct', 'time', 'thread', 'cStringIO', 'cPickle', '_locale', 'marshal', 'imp',
            '__main__', '__builtin__', 'sys']
table = bc_test.inittab()
check('module table content and order ' + repr(table), table == expected)

names = list(sys.builtin_module_names)
sorted_expected = expected[:]
sorted_expected.sort()
check('sys.builtin_module_names is the sorted table ' + repr(names), names == sorted_expected)

absent = ['audioop', 'imageop', 'md5', 'rgbimg', 'rotor', 'signal', 'sha', 'soundex', 'pcre', 'msvcrt',
          'winsound', 'posix', '_socket', 'select', 'Appc']
present = []
for name in absent:
    if name in names:
        present.append(name)
check('no other module is built in ' + repr(present), present == [])

# cPickle needs copy_reg and string while it starts. The scripts of the game folder give them in the exe.
stub = new.module('copy_reg')
stub.dispatch_table = {}
stub.safe_constructors = {}
sys.modules['copy_reg'] = stub
stub = new.module('string')
stub.atol = strop.atol
sys.modules['string'] = stub
failed = []
for name in expected[:-3]:
    try:
        __import__(name)
    except ImportError:
        failed.append(name)
check('every built-in module imports ' + repr(failed), failed == [])
