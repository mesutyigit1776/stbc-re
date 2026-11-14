# The font manager as the script Icons.FontsAndIcons uses it: the real script of the game folder registers 14 fonts and 15
# icon groups. Each font registers an icon group with the name of the font and the size with two decimals, the module
# and the function of the script that loads it. The loader modules are replaced by modules that note their call, so the
# icon manager shows what the script registered. The host runs this file with `bc_pyhost GAME_ROOT --run fonts.py`.
import sys, new, string, App, Appc

failures = []

def check(name, ok):
    if ok:
        print 'PASS', name
    else:
        print 'FAIL', name
        failures.append(name)

def raised(func, *args):
    try:
        apply(func, args)
    except:
        return sys.exc_info()
    return None

calls = []

def loader(module, function):
    mod = new.module(module)
    setattr(mod, function, lambda m=module, f=function: calls.append((m, f)))
    sys.modules[module] = mod

def run(name):
    """The calls the icon manager makes for the registered name, as (module, function) pairs."""
    del calls[:]
    group = App.g_kIconManager.GetIconGroup(name)
    return list(calls), group

fm = App.g_kFontManager
check('the module attribute gives a font manager', fm is not None)
before = Appc.globals.g_kFontManager
info = raised(setattr, Appc.globals, 'g_kFontManager', 5)
check('the module attribute takes no integer', info is not None)
Appc.globals.g_kFontManager = before
check('the module attribute keeps the object', Appc.globals.g_kFontManager == before and fm.this == before)

# One font by hand: the group name is the font name and the size with two decimals.
loader('FontProbe', 'LoadProbe')
check('a font that is not registered has no icon group', run('Probe9.50')[0] == [])
fm.RegisterFont('Probe', 9.5, 'FontProbe', 'LoadProbe')
check('the registered font runs its loader through the icon manager', run('Probe9.50')[0] == [('FontProbe', 'LoadProbe')])
check('the loader module is called, not the function name', run('Probe9.50')[0][0][0] == 'FontProbe')
fm.RegisterFont('Probe', 12, 'FontProbe', 'LoadProbe')
check('an integer size is accepted and has two decimals', run('Probe12.00')[0] == [('FontProbe', 'LoadProbe')])
check('the size 9.5 has the name with the decimals 50', run('Probe9.5')[0] == [])
fm.RegisterFont('Probe', 0.125, 'FontProbe', 'LoadProbe')
check('a tie in the second decimal rounds as the C run time of the game does', run('Probe0.13')[0] == [('FontProbe', 'LoadProbe')])

# The values that are not strings stop in the wrapper with the error text of the exe.
info = raised(fm.RegisterFont, 5, 5, 'FontProbe', 'LoadProbe')
check('a name that is not a string is an error', info is not None and string.find(str(info[1]), 'Expected None or string') >= 0)
info = raised(fm.RegisterFont, 'Probe', 5, 1, 'LoadProbe')
check('a module that is not a string is an error', info is not None and string.find(str(info[1]), 'Expected None or string') >= 0)
info = raised(fm.RegisterFont, 'Probe', 'big', 'FontProbe', 'LoadProbe')
check('a size that is not a number is an error', info is not None)

# The real script. The loader modules are replaced first.
FONTS = [('Crillee', '5', 'Crillee5', 'LoadCrillee5'), ('Crillee', '6', 'Crillee6', 'LoadCrillee6'),
         ('Crillee', '9', 'Crillee9', 'LoadCrillee9'), ('Crillee', '12', 'Crillee12', 'LoadCrillee12'),
         ('Crillee', '15', 'Crillee15', 'LoadCrillee15'), ('LCARSText', '5', 'LCARSText5', 'LoadLCARSText5'),
         ('LCARSText', '6', 'LCARSText6', 'LoadLCARSText6'), ('LCARSText', '9', 'LCARSText9', 'LoadLCARSText9'),
         ('LCARSText', '12', 'LCARSText12', 'LoadLCARSText12'), ('LCARSText', '15', 'LCARSText15', 'LoadLCARSText15'),
         ('Tahoma', '8', 'Tahoma', 'LoadTahoma'), ('Tahoma', '14', 'TahomaLarge', 'LoadTahomaLarge'),
         ('Arial', '8', 'Arial', 'LoadArial'), ('Serpentine', '12', 'Serpentine12', 'LoadSerpentine12')]
ICONS = [('Console', 'Console', 'LoadConsole'), ('LCARS_640', 'LCARS_640', 'LoadLCARS_640'),
         ('LCARS_800', 'LCARS_800', 'LoadLCARS_800'), ('LCARS_1024', 'LCARS_1024', 'LoadLCARS_1024'),
         ('LCARS_1280', 'LCARS_1280', 'LoadLCARS_1280'), ('LCARS_1600', 'LCARS_1600', 'LoadLCARS_1600'),
         ('ShipIcons', 'ShipIcons', 'LoadShipIcons'), ('DamageIcons', 'DamageIcons', 'LoadDamageIcons'),
         ('SplashScreen', 'SplashScreen', 'LoadSplashScreen'), ('Frame', 'Frame', 'LoadFrame'),
         ('System', 'SystemIcons', 'LoadSystemIcons'), ('FrameButton', 'FrameButton', 'LoadFrameButton'),
         ('TargetArrow', 'TargetArrow', 'LoadTargetArrows'), ('WeaponIcons', 'WeaponIcons', 'LoadWeaponIcons'),
         ('NormalStyleFrame', 'StylizedWindow', 'LoadNormalStyleFrame')]
for font in FONTS:
    loader(font[2], font[3])
for icon in ICONS:
    loader(icon[1], icon[2])

sys.path.append('scripts/Icons')
info = raised(__import__, 'FontsAndIcons')
check('the script FontsAndIcons imports', info is None)
if info is not None:
    print info
for font in FONTS:
    name = font[0] + font[1] + '.00'
    check('the script registers %s with %s.%s' % (name, font[2], font[3]), run(name)[0] == [(font[2], font[3])])
for icon in ICONS:
    check('the script registers the icon group %s with %s.%s' % (icon[0], icon[1], icon[2]), run(icon[0])[0] == [(icon[1], icon[2])])
check('a name with a size that the script does not register is not registered', run('Crillee7.00')[0] == [])
check('the name of a family alone is not registered', run('Crillee')[0] == [])

if failures:
    print 'fonts: %d checks failed' % len(failures)
else:
    print 'fonts: all checks passed'
