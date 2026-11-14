# Every constant of the Appc module has the value that target/appc_constants.tsv holds.
import sys, strop, Appc

tsv = sys.argv[1]
f = open(tsv, 'r')
lines = strop.split(f.read(), '\n')
f.close()

count = 0
wrong = []
for line in lines[1:]:
    if line == '':
        continue
    fields = strop.split(line, '\t')
    name = fields[0]
    kind = fields[2]
    count = count + 1
    if not hasattr(Appc, name):
        wrong.append(name + ' (missing)')
        continue
    value = getattr(Appc, name)
    if kind == 'int':
        expected = int(fields[1])
        if type(value) != type(1) or value != expected:
            wrong.append(name)
    else:
        expected = float(fields[1])
        if type(value) != type(1.0) or value != expected:
            wrong.append(name)

check('constants: 1809 rows in the table', count == 1809)
if wrong:
    print 'wrong constants:', wrong[:10]
check('constants: all values equal to the table', len(wrong) == 0)
