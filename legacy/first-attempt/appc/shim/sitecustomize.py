# Test harness only: the engine attaches the hand-written App methods right after the first `import App`
# (UtopiaModule::Initialize registers them; swigImportModule flushes the queue). The real engine host does the same call.
import __builtin__
import sys

_orig_import = __builtin__.__import__
_attached = [0]
_busy = [0]

def _import(name, *args):
    # forward exactly the arguments given: __import__('a.b') (no fromlist) must keep the original's leaf-module behavior
    if not args:
        # the TG __import__ uses the caller's globals when none are given (relative imports inside packages, e.g.
        # AI.Compound.BasicAttack doing __import__("FedAttack")); pass the real caller's globals, not this wrapper's
        try:
            raise ZeroDivisionError
        except ZeroDivisionError:
            args = (sys.exc_info()[2].tb_frame.f_back.f_globals,)
    mod = apply(_orig_import, (name,) + args)
    if name == 'App' and not _attached[0] and sys.modules.has_key('App'):
        _attached[0] = 1
        import Appc
        Appc.attach_extras(sys.modules['App'])
    if sys.modules.has_key('Tactical.Interface.WeaponsDisplay') and not sys.modules.has_key('lcars_fit_done') and not _busy[0]:
        # the stock tables keyed by mode width get entries for a wide mode (port only, shim/lcars_fit.py)
        _busy[0] = 1
        try:
            import lcars_fit
            lcars_fit.fit_tables()
        except:
            pass
        _busy[0] = 0
    if name[:17] == 'ships.Hardpoints.' and sys.modules.has_key(name) and not _busy[0]:
        # mod hardpoints with the arc limits the wrong way round get them swapped (port only, shim/hardpoint_arcs.py)
        _busy[0] = 1
        try:
            import hardpoint_arcs
            hardpoint_arcs.fix_module(sys.modules[name])
        except:
            pass
        _busy[0] = 0
    if name[:11] == 'Icons.LCARS' and sys.modules.has_key(name):
        # the widths of the LCARS module follow the width of the graphics mode (port only, shim/lcars_fit.py)
        try:
            import lcars_fit
            lcars_fit.fit_module(sys.modules[name])
        except:
            pass
    return mod

__builtin__.__import__ = _import

# the loaders run `reload(mod)` on the hardpoint module just before LoadPropertySet (loadspacehelper), which makes the properties again
_orig_reload = __builtin__.reload
def _reload(mod):
    mod = _orig_reload(mod)
    try:
        if mod.__name__[:17] == 'ships.Hardpoints.':
            import hardpoint_arcs
            hardpoint_arcs.fix_module(mod)
    except:
        pass
    return mod
__builtin__.reload = _reload
