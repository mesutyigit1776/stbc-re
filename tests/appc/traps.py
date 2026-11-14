# A method or a module attribute that is not reconstructed raises an exception with its name and exe address.
import sys, Appc, bc_test

def raised(func, *args):
    try:
        apply(func, args)
    except:
        return sys.exc_info()
    return None

info = raised(Appc.TGLocDBWrapperSerialize)
check('trap method raises', info is not None)
check('trap method text', info is not None and info[1] == 'Appc.TGLocDBWrapperSerialize is not reconstructed (exe wrapper 0x5bc220)')
info = raised(Appc.TGLocDBWrapperUnserialize, 1, 2)
check('trap method ignores its arguments', info is not None and info[1] == 'Appc.TGLocDBWrapperUnserialize is not reconstructed (exe wrapper 0x5bc2d0)')

value = Appc.globals.g_kSystemWrapper
check('trap attribute read gives a value', value is not None)
info = raised(getattr, value, 'GetName')
check('trap attribute use raises', info is not None)
check('trap attribute text', info is not None and info[1] == 'Appc.globals.g_kSystemWrapper is not reconstructed (exe getter 0x65b180)')
check('trap attribute repr', repr(value) == '<Appc.globals.g_kSystemWrapper not reconstructed (exe getter 0x65b180)>')

def setvalue():
    Appc.globals.g_kSystemWrapper = 5
info = raised(setvalue)
check('trap attribute write raises', info is not None)
check('trap attribute write text', info is not None and info[1] == 'Appc.globals.g_kSystemWrapper is not reconstructed (exe setter 0x65b140)')

def unknown():
    return Appc.globals.no_such_variable
info = raised(unknown)
check('unknown attribute of globals', info is not None and info[1] == 'Unknown C global variable')

names = bc_test.variable_names()
check('135 module attributes', len(names) == 135)
check('first module attribute', names[0] == 'g_kSystemWrapper')
check('last module attribute', names[-1] == 'FloatRangeWatcher_RID_INVALID')
check('repr of globals', repr(Appc.globals) == '<Global variables>')
