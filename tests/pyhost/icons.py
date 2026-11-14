# The icon classes as the scripts use them: the real script ShipIcons of the game loads a group of 17 icons with the
# calls CreateIconGroup, AddIconGroup, LoadIconTexture and SetIconLocation. The texture sizes come from the TGA files of
# the game folder. The host runs this file with `bc_pyhost GAME_ROOT --run icons.py`.
import sys, new, App, Appc

failures = []

def check(name, ok):
    if ok:
        print 'PASS', name
    else:
        print 'FAIL', name
        failures.append(name)

def near(a, b):
    d = a - b
    if d < 0:
        d = -d
    return d < 0.000001

def raised(func, *args):
    try:
        apply(func, args)
    except:
        return sys.exc_info()
    return None

sys.path.append('scripts/Icons')
mgr = App.g_kIconManager

# The new manager.
check('a new manager has the pixel center adjustment -0.5', mgr.GetPixelCenterAdj() == -0.5)
check('a new manager has the scale 0', mgr.GetScale() == 0.0)
mgr.SetScale(1.5)
mgr.SetScreenWidth(640.0)
mgr.SetScreenHeight(480.0)
check('the scale is kept', mgr.GetScale() == 1.5)
check('the screen width is kept', mgr.GetScreenWidth() == 640.0)
check('the screen height is kept', mgr.GetScreenHeight() == 480.0)
mgr.SetPixelCenterAdj(0.25)
check('the pixel center adjustment is kept', mgr.GetPixelCenterAdj() == 0.25)
check('a manager without a camera gives a camera pointer', mgr.GetCamera() is not None)

# The module attribute of the engine checks the type and stores nothing.
before = Appc.globals.g_kIconManager
info = raised(setattr, Appc.globals, 'g_kIconManager', 5)
check('the module attribute takes no integer', info is not None)
Appc.globals.g_kIconManager = before
check('the module attribute keeps the object', Appc.globals.g_kIconManager == before and mgr.this == before)

# A group that is not registered is not found; a registered group loads on demand.
check('a group that is not registered is not found', mgr.GetIconGroup('ShipIcons') is None)
mgr.RegisterIconGroup('ShipIcons', 'ShipIcons', 'LoadShipIcons')
group = mgr.GetIconGroup('ShipIcons')
check('the registered group loads on demand', group is not None)
check('the group has its name', group.GetName() == 'ShipIcons')
again = mgr.GetIconGroup('ShipIcons')
check('the loaded group is found again', again is not None and again.this == group.this)
check('the first icon has the size of its file', group.GetIconPixelWidth(App.SPECIES_UNKNOWN) == 8.0 and
      group.GetIconPixelHeight(App.SPECIES_UNKNOWN) == 8.0)
check('the screen size follows the manager', near(group.GetIconScreenWidth(App.SPECIES_UNKNOWN), 1.5 * 8.0 / 640.0) and
      near(group.GetIconScreenHeight(App.SPECIES_UNKNOWN), 1.5 * 8.0 / 480.0))
check('the icon of a ship is known', group.IsNumValid(App.SPECIES_GALAXY) == 1)
check('an icon that was not set is not known', group.IsNumValid(App.SPECIES_GALAXY + 100000) == 0)
check('the size of an unknown icon is 0', group.GetIconPixelWidth(App.SPECIES_GALAXY + 100000) == 0.0)
count = group.GetCount()
check('the group has the icons of the script', count > 10)

# The walk over the icon numbers visits each number once.
group.GoToFirstPos()
seen = {}
for i in range(count):
    number = group.GetNext()
    seen[number] = 1
check('the walk visits each number once', len(seen.keys()) == count)
check('the walk gives valid numbers', len(filter(lambda n, g=group: g.IsNumValid(n), seen.keys())) == count)

# A group that a script makes: the rotation swaps the size of an icon, the texture handle counts the loads.
test = mgr.CreateIconGroup('Test')
check('a new group has its name', test.GetName() == 'Test' and test.GetCount() == 0)
first = test.LoadIconTexture('Data/Icons/Bridge/Background/ScreenBlock.tga')
second = test.LoadIconTexture('Data/Icons/Ships/Galaxy.tga', 1)
missing = test.LoadIconTexture('Data/Icons/Nope.tga')
check('the texture handles count the loads', first == 0 and second == 1 and missing == -1)
test.SetIconLocation(1, first, 2.0, 4.0, 3.0, 5.0)
test.SetIconLocation(2, first, 2.0, 4.0, 3.0, 5.0, 1)
test.SetIconLocation(3, second, 0.0, 0.0, 16.0, 16.0, 2, Appc.TGIconGroup_MIRROR_HORIZONTAL)
check('an icon without a rotation keeps its size', test.GetIconPixelWidth(1) == 3.0 and test.GetIconPixelHeight(1) == 5.0)
check('the rotation 1 swaps the size', test.GetIconPixelWidth(2) == 5.0 and test.GetIconPixelHeight(2) == 3.0)
check('the rotation 2 keeps the size', test.GetIconPixelWidth(3) == 16.0 and test.GetIconPixelHeight(3) == 16.0)
check('the group counts its icons', test.GetCount() == 3)
check('a texture of the same file again gives the next handle', test.LoadIconTexture('Data/Icons/Ships/Galaxy.tga') == 2)
test.SetName('Renamed')
check('SetName changes the name', test.GetName() == 'Renamed')
test.SetName(None)
check('a group without a name gives None', test.GetName() is None)
test.SetName('Test')
test.Reset()
check('Reset empties the group', test.GetCount() == 0 and test.IsNumValid(1) == 0)

# A group that is created and added is found only when it is registered.
mgr.AddIconGroup(test)
check('a group without a registration is not found', mgr.GetIconGroup('Test') is None)
module = new.module('IconsTestModule')
exec 'calls = 0\ndef Load():\n    global calls\n    calls = calls + 1\n' in module.__dict__
sys.modules['IconsTestModule'] = module
mgr.RegisterIconGroup('Test', 'IconsTestModule', 'Load')
found = mgr.GetIconGroup('Test')
check('a group that is loaded does not run its function', found is not None and found.this == test.this and module.calls == 0)
check('Purge deletes the groups that are not locked', mgr.Purge() is None)
check('the purged group is loaded again by its function', mgr.GetIconGroup('Test') is None and module.calls == 1)
check('DeleteIconGroup finds the loaded group', mgr.DeleteIconGroup('ShipIcons') == 1)
check('DeleteIconGroup does not find it again', mgr.DeleteIconGroup('ShipIcons') == 0)
check('the deleted group is gone', mgr.GetIconGroup('ShipIcons') is None)

# A group made with the class constructor.
named = App.TGIconGroup('Named')
check('the class constructor sets the name', named.GetName() == 'Named')

# The wrappers raise the exception of the exe for a wrong argument.
info = raised(mgr.GetIconGroup, 5)
check('a text argument of another type raises a TypeError', info is not None and info[0] is TypeError and
      info[1] == 'Expected None or string, int found')

if failures:
    raise 'icons: %d checks failed' % len(failures)
print 'icons: all checks passed'
