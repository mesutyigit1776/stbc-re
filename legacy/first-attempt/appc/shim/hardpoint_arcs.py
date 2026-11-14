# Port only: a fix layer for mod hardpoints whose weapon arcs are written with the limits the wrong way round.
# PhaserBank::IsAngleInArc 00571ee0 (and the pulse weapon and tractor versions) accept an angle when min <= angle < max, so a bank with
# min > max can never fire. The Orion Delta ship packs have such banks (the Constitution's forward, port and starboard banks have the
# height arc 1.396 to 0.0 and 0.0 to -1.396, the upper and lower banks of the author's tool, written as "from 80 degrees down to 0").
# The original game has the same bank that never fires. This layer swaps the limits so the arc is the one the author meant. The stock
# game's own hardpoints (their .pyc files come from the game folder) are left as the original has them.
import os, string

_KINDS = ('PhaserProperty', 'PulseWeaponProperty', 'TractorBeamProperty')

def _is_stock(mod):
    # the stock hardpoint modules are the symlinks into the game folder (the last entry of APPC_DATA_ROOTS)
    f = getattr(mod, '__file__', None)
    roots = os.environ.get('APPC_DATA_ROOTS')
    if not f or not roots:
        return 0
    game = string.split(roots, ':')[-1]
    for i in range(20):
        if string.find(f, game + '/') == 0:
            return 1
        try:
            f = os.readlink(f)
        except (os.error, AttributeError):
            break
    return 0

def _fix_prop(o):
    n = 0
    lo, hi = o.GetArcHeightAngleMin(), o.GetArcHeightAngleMax()
    if lo > hi:
        o.SetArcHeightAngles(hi, lo)
        n = n + 1
    lo, hi = o.GetArcWidthAngleMin(), o.GetArcWidthAngleMax()
    if lo > hi:
        o.SetArcWidthAngles(hi, lo)
        n = n + 1
    return n

def fix_module(mod):
    # the module's property objects, and the copies the property manager made when the script registered them as local templates
    if _is_stock(mod):
        return 0
    import App
    n = 0
    for name in dir(mod):
        o = getattr(mod, name)
        try:
            ok = hasattr(o, 'GetArcHeightAngleMin') and hasattr(o, 'SetArcHeightAngles') and hasattr(o, 'SetArcWidthAngles')
        except:
            ok = 0
        if not ok:
            continue
        try:
            n = n + _fix_prop(o)
            t = App.g_kModelPropertyManager.FindByName(o.GetName(), App.TGModelPropertyManager.LOCAL_TEMPLATES)
            if t is not None and hasattr(t, 'GetArcHeightAngleMin'):
                n = n + _fix_prop(t)
        except:
            pass
    return n
