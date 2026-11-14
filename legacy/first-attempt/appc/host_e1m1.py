# Headless E1M1 run: start the mission, run frames, and act as the player where the tutorial waits for input.
# Prints each E1M1 function the first time it runs (the mission flow).
import sys
import Appc
import string, os
ALL = os.environ.get('E1M1_ALL')
execfile('../port/appc/host_boot.py')
import App

seen = {}
def prof(frame, event, arg):
    if event == 'call':
        c = frame.f_code
        fn = c.co_filename
        i = max(string.rfind(fn, '/'), string.rfind(fn, '\\'))
        key = fn[i+1:] + ':' + c.co_name
        if (ALL or fn[i+1:i+6] == 'E1M1.') and not seen.has_key(key):
            seen[key] = 1
            print '[%7.1f] %s' % (t, key)
t = 0.0
if os.environ.get('E1M1_PROF'): sys.setprofile(prof)

name = App.TGString()
name.SetString('Test Captain')
App.g_kUtopiaModule.SetCaptainName(name)
App.Game_SetDifficulty(1)
App.g_kVarManager.SetStringVariable('Options', 'EpisodeOverride', 'Episode1')
App.g_kVarManager.SetStringVariable('Options', 'MissionOverride', 'E1M1')
game = Appc.engine_new_game('Maelstrom.Maelstrom')

def run(secs):
    global t
    n = int(secs * 10)
    for i in range(n):
        t = t + 0.1
        Appc.engine_tick(t, t)

def mod():
    return sys.modules.get('Maelstrom.Episode1.E1M1.E1M1')
def run_until(test, limit):
    global t
    end = t + limit
    while t < end and not test():
        run(0.5)
    return test()
def send(etype, dest, value = None, kind = 'TGBoolEvent'):
    e = getattr(App, kind + '_Create')()
    e.SetEventType(etype)
    if dest: e.SetDestination(dest)
    if value is not None:
        if kind == 'TGBoolEvent': e.SetBool(value)
        else: e.SetInt(value)
    App.g_kEventManager.AddEvent(e)
def step(name, ok):
    print '[%7.1f] player step: %-34s %s' % (t, name, ok and 'ok' or 'NOT REACHED')
    return ok

TOTAL = 400.0
if len(sys.argv) > 1: TOTAL = float(sys.argv[1])
if TOTAL > 0:
    run(TOTAL)
else:
    # Act as the player where the tutorial waits for input (the original game gets these events from mouse clicks).
    pass
    if step('intro and inspection', run_until(lambda: mod() and mod().g_bInCharacterSelection, 400)):
        send(App.ET_CHARACTER_MENU, mod().g_pSaffi, 1); run(2)           # click Saffi: her menu opens
        send(App.ET_CHARACTER_MENU, mod().g_pSaffi, 0); run(1)           # click again: it closes -> Objectives()
        step('objectives suggested', run_until(lambda: mod().g_bInObjectives, 30))
        run(15)
        send(App.ET_REPORT_GOAL_INFO, None, 0, 'TGIntEvent')         # the player checks the objective -> CheckTacticalView()
        step('tactical view suggested', run_until(lambda: mod().g_bInTacticalView, 30))
        run(15)
        top = App.TopWindow_GetTopWindow()
        send(App.ET_INPUT_TOGGLE_BRIDGE_AND_TACTICAL, top, 1); run(3)   # to tactical
        send(App.ET_INPUT_TOGGLE_BRIDGE_AND_TACTICAL, top, 1); run(3)   # back to the bridge -> WarpSequence()
        step('warp suggested', run_until(lambda: mod().g_bSuggestWarpDone, 30))
        run(20)
        # Set course to Starbase 12 in Kiska's menu, then press Warp (the original game gets these from mouse clicks)
        db = App.g_kLocalizationManager.Load('data/TGL/Bridge Menus.tgl')
        helm = App.TacticalControlWindow_GetTacticalControlWindow().FindMenu(db.GetString('Helm'))
        course = helm and helm.GetSubmenuW(db.GetString('Set Course'))
        App.g_kLocalizationManager.Unload(db)
        sysdb = App.g_kLocalizationManager.Load('data/TGL/Systems.TGL')
        sb12 = course and course.GetSubmenuW(sysdb.GetString('Starbase 12'))
        App.g_kLocalizationManager.Unload(sysdb)
        send(App.ET_CHARACTER_MENU, mod().g_pKiska, 1); run(1)             # click Kiska: the helm menu opens
        if course: Appc.engine_click(course); run(1)                       # open Set Course
        if sb12: Appc.engine_click(sb12); run(1)                           # pick Starbase 12
        wb = App.SortedRegionMenu_GetWarpButton()
        step('course set to Starbase 12', wb and wb.GetDestination() == 'Systems.Starbase12.Starbase12')
        Appc.engine_click(wb); run(1)                                      # press Warp
        step('warp started', run_until(lambda: App.Game_GetCurrentPlayer().GetContainingSet().GetName() == 'warp', 30))
        step('arrived at Starbase 12', run_until(lambda: mod().g_bPlayerArriveStarbase, 60))
        run(30)
        # target the starbase in the target list, then Intercept in Kiska's menu flies the ship close enough to dock
        tm = App.STTargetMenu_GetTargetMenu()
        sb = App.ShipClass_GetObject(App.g_kSetManager.GetSet('Starbase12'), 'Starbase 12')
        entry = tm and sb and tm.GetObjectEntry(sb)
        if entry: Appc.engine_click(entry); run(1)
        step('starbase targeted', App.Game_GetCurrentPlayer().GetTarget() and App.Game_GetCurrentPlayer().GetTarget().GetName() == 'Starbase 12')
        db = App.g_kLocalizationManager.Load('data/TGL/Bridge Menus.tgl')
        intercept = helm and helm.GetButtonW(db.GetString('Intercept'))
        App.g_kLocalizationManager.Unload(db)
        if intercept: Appc.engine_click(intercept); run(1)                 # Intercept: the helm flies to the target
        step('close to Starbase 12', run_until(lambda: mod().g_bPlayerCloseToStarbase, 240))
        step('Graff hailed, dock enabled', run_until(lambda: mod().g_bGraffHailed, 120))
        run(5)
        import Bridge.BridgeUtils
        dock = Bridge.BridgeUtils.GetDockButton()
        if dock: Appc.engine_click(dock); run(1)                           # Dock
        step('docking: supplies goal given', run_until(lambda: mod().g_bSuppliesReceived, 30))
        run(60)
sys.setprofile(None)
E = mod()
if E:
    for k in ('g_bIntrosDone', 'g_bDryDockUndock', 'g_bInCharacterSelection', 'g_bInObjectives', 'g_bPlayerArriveStarbase', 'g_bSuppliesReceived', 'g_iMissionState'):
        print k, '=', getattr(E, k, '?')
p = App.Game_GetCurrentPlayer()
if p:
    ps = p.GetPowerSubsystem()
    if ps:
        print 'player power: main %.0f backup %.0f output %.0f conduit %.0f' % (ps.GetMainBatteryPower(), ps.GetBackupBatteryPower(), ps.GetPowerOutput(), ps.GetMainConduitCapacity())
    for s in (p.GetShields(), p.GetImpulseEngineSubsystem(), p.GetSensorSubsystem()):
        if s: print '  %s: wanted %.2f received %.2f pct %.2f' % (s.GetName(), s.GetPowerWanted(), s.GetPowerReceived(), s.GetPowerPercentage())
    sen = p.GetSensorSubsystem()
    if sen:
        s = p.GetContainingSet()
        print '  sensor range %.0f; objects in %s:' % (sen.GetSensorRange(), s.GetName())
        for o in s.GetClassObjectList(App.CT_SHIP):
            if o.GetObjID() != p.GetObjID():
                a = o.GetWorldLocation(); b = p.GetWorldLocation(); a.Subtract(b)
                print '    %-16s dist %7.1f near %d far %d known %d visible %d' % (o.GetName(), a.Length(),
                    sen.IsObjectNear(o, 0), sen.IsObjectFar(o, 0), sen.IsObjectKnown(o), sen.IsObjectVisible(o))
rd = App.TacticalControlWindow_GetTacticalControlWindow().GetRadarDisplay()
if rd:
    rs = App.TGPane_Cast(App.TGPane_Cast(rd).GetNthChild(0)); bp = App.TGPane_Cast(rs.GetNthChild(4))
    print 'radar blips:', bp.GetNumChildren()
    for i in range(bp.GetNumChildren()):
        b = bp.GetNthChild(i)
        print '  blip at %.3f, %.3f (scope %.3f x %.3f)' % (b.GetLeft(), b.GetTop(), rs.GetWidth(), rs.GetHeight())
