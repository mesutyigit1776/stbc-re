# Headless host: mimics the start-up order of stbc.exe (report G topic 08, UtopiaModule::Initialize 0x43ad70 and FUN_0043b2f0).
# Run from the game folder: PYTHONPATH=scripts:<shim> python152_appc host_boot.py
import sys
import Appc

steps = []

def step(label, func, *args):
    try:
        r = apply(func, args)
        steps.append((label, 'ok'))
        return r
    except:
        t, v = sys.exc_info()[:2]
        steps.append((label, 'FAIL %s: %s' % (t, v)))
        return None

def call(modname, funcname, *args):
    mod = __import__(modname)          # leaf module, like the original's swigImportModule
    return apply(getattr(mod, funcname), args)

step('engine_init', Appc.engine_init)
step('import App', __import__, 'App')
step('FixApp.FixApp', call, 'FixApp', 'FixApp')
step('import Autoexec', __import__, 'Autoexec')
step('loadsplash.RemoveSplashScreen', call, 'loadsplash', 'RemoveSplashScreen')
step('VarManager.SetStringVariable', lambda: sys.modules['App'].g_kVarManager.SetStringVariable('Multiplayer', 'Mission', 'Multiplayer.Episode.Mission1.Mission1'))
step('Display.Initialize', call, 'Display', 'Initialize')
step('MainMenu.mainmenu.LoadConfigPart1', call, 'MainMenu.mainmenu', 'LoadConfigPart1')

for label, res in steps:
    print '%-42s %s' % (label, res)
