# Ship loading test: create every ship script of the scripts folder with loadspacehelper.CreateShip (the same path as the missions)
# and check the result: subsystems from the hardpoint file and the model files the ship script names.
# Run: port/appc/run.sh ../port/appc/test_ships.py            (stock game)
#      R=$PWD; APPC_ROOT=$R/port/appc/overlay_remastered APPC_OVERLAY=scripts APPC_DATA_ROOTS=$R/mods/remastered_1.2:$R/game \
#          port/appc/run.sh $R/port/appc/test_ships.py       (a mod: run from its merged root, data from the mod first, then the game)
import sys, os, string
import Appc
execfile(os.path.join(os.path.dirname(sys.argv[0]), 'host_boot.py'))
import App

roots = []
if os.environ.has_key('APPC_DATA_ROOTS'): roots = filter(None, string.split(os.environ['APPC_DATA_ROOTS'], ':'))
roots.append('.')
def exists(path):
    p = string.replace(path, '\\', '/')
    for r in roots:
        if os.path.exists(os.path.join(r, p)): return 1
    return 0

sdir = string.split(os.environ['PYTHONPATH'], ':')[0] + '/ships'
names = {}
for f in os.listdir(sdir):
    b, e = os.path.splitext(f)
    if e in ('.py', '.pyc') and b != '__init__': names[b] = 1
names = names.keys(); names.sort()
pSet = App.SetClass_Create()
App.g_kSetManager.AddSet(pSet, 'ShipTest')
import loadspacehelper
ok = 0; bad = []; nomodel = []; nohull = []; counts = {}; noradius = []; radii = []
def one(n):
    global ok
    mod = __import__('ships.' + n)
    mod = sys.modules['ships.' + n]
    if not hasattr(mod, 'GetShipStats'): return
    s = mod.GetShipStats()
    pShip = loadspacehelper.CreateShip(n, pSet, 'test_' + n, None, 0, 0)
    if not pShip:
        bad.append((n, 'CreateShip returned None'))
        return
    ok = ok + 1
    files = []
    for k in ('FilenameHigh', 'FilenameMed', 'FilenameLow'):
        if s.has_key(k) and s[k]: files.append(s[k])
    missing = filter(lambda f: not exists(f), files)
    if missing: nomodel.append((n, missing[0]))
    if not pShip.GetHull(): nohull.append(n)
    if pShip.GetRadius() == 1.0: noradius.append(n)
    radii.append((pShip.GetRadius(), n))
    for g, nm in ((pShip.GetPhaserSystem(), 'phasers'), (pShip.GetTorpedoSystem(), 'torpedo tubes'), (pShip.GetPulseWeaponSystem(), 'pulse weapons'), (pShip.GetTractorBeamSystem(), 'tractors')):
        if g: counts[nm] = counts.get(nm, 0) + g.GetNumChildSubsystems()
for n in names:
    try:
        one(n)
    except:
        t, v = sys.exc_info()[:2]
        bad.append((n, '%s: %s' % (t, v)))
print 'ship scripts:', len(names), ' created:', ok, ' failed:', len(bad), ' missing model file:', len(nomodel), ' no hull:', len(nohull)
print 'weapons (all ships):', counts
radii.sort()
print 'model radius from NIF:', len(radii) - len(noradius), 'of', len(radii), ' smallest', radii[:1], ' largest', radii[-1:]
for n, why in bad[:25]: print 'FAIL', n, why
for n, f in nomodel[:10]: print 'NO MODEL', n, f
