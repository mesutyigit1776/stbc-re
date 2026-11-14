def read_first_line(name):
    f = open(name)
    line = f.readline()
    f.close()
    return line

check('open works outside PyRun_SimpleString', read_first_line('other/file.txt') == 'other file\n')
check('counter is 0 outside', bc_test.securelevel() == 0)

status = bc_test.run_simple(
    "import bc_test\n"
    "inside_level = bc_test.securelevel()\n"
    "try:\n"
    "    open('other/file.txt')\n"
    "    inside_read = 'opened'\n"
    "except IOError, msg:\n"
    "    inside_read = msg\n"
    "try:\n"
    "    open('other/new.txt', 'w')\n"
    "    inside_write = 'opened'\n"
    "except IOError, msg:\n"
    "    inside_write = msg\n"
    "try:\n"
    "    open()\n"
    "    inside_noargs = 'opened'\n"
    "except IOError, msg:\n"
    "    inside_noargs = msg\n"
    "except TypeError, msg:\n"
    "    inside_noargs = 'TypeError'\n")
check('the script run returns 0', status == 0)
check('counter is above 0 inside', inside_level == 1)
check('read open is refused inside', inside_read == 'Securelevel prevents file IO.')
check('write open gives the file IO text (B0009)', inside_write == 'Securelevel prevents file IO.')
check('open with no arguments raises IOError (B0009)', inside_noargs == 'Securelevel prevents file IO.')
check('counter is 0 after the run', bc_test.securelevel() == 0)
check('open works after the run', read_first_line('other/file.txt') == 'other file\n')

status = bc_test.run_simple("1 / 0\n")
check('a failing script returns -1', status == -1)
check('counter is 0 after a failing run', bc_test.securelevel() == 0)

status = bc_test.run_simple(
    "import bc_test\n"
    "bc_test.run_simple('nested_level = bc_test.securelevel()\\n')\n"
    "after_inner = bc_test.securelevel()\n")
check('the inner run is at securelevel', nested_level == 1)
check('securelevel stays on after the inner run', after_inner == 1)
check('counter is 0 after the nested runs', bc_test.securelevel() == 0)
