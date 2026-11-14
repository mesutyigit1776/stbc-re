# The method table has every name of target/appc_method_table.tsv, in the same order.
import sys, strop, Appc, bc_test

f = open(sys.argv[1], 'r')
lines = strop.split(f.read(), '\n')
f.close()
expected = []
for line in lines[1:]:
    if line != '':
        expected.append(strop.split(line, '\t')[2])

check('methods: 3990 rows in the table', len(expected) == 3990)
names = bc_test.method_names()
check('methods: the module table has the exe order', names == expected)

missing = []
for name in expected:
    if not hasattr(Appc, name):
        missing.append(name)
if missing:
    print 'missing:', missing[:10]
check('methods: every name is an attribute of Appc', len(missing) == 0)

builtin = type(Appc.IsNull)
notfunc = []
for name in expected:
    if hasattr(Appc, name) and type(getattr(Appc, name)) != builtin:
        notfunc.append(name)
check('methods: every name is a built-in function', len(notfunc) == 0)
