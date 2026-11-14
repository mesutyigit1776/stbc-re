# Ship systems check: power, repair, sensors, cloak, alert levels on a Galaxy and a Bird of Prey.
import sys, os
import Appc
execfile(os.path.join(os.path.dirname(sys.argv[0]), 'host_boot.py'))
import App, loadspacehelper
pSet = App.SetClass_Create(); App.g_kSetManager.AddSet(pSet, 'SysTest')
pA = loadspacehelper.CreateShip('Galaxy', pSet, 'Galaxy', None, 0, 0)
pB = loadspacehelper.CreateShip('BirdOfPrey', pSet, 'Bird', None, 0, 0)
p = App.TGPoint3(); p.SetXYZ(0.0, 300.0, 0.0); pB.SetTranslate(p)
t = [0.0]
def run(secs):
    for i in range(int(secs * 10)):
        t[0] = t[0] + 0.1
        Appc.engine_tick(t[0], t[0])
ev = []
def Note(pObject, pEvent):
    ev.append(pEvent.GetEventType())
    pObject.CallNextHandler(pEvent)
for e in ('ET_REPAIR_COMPLETED', 'ET_SUBSYSTEM_COMPLETELY_DISABLED', 'ET_SUBSYSTEM_PARTIALLY_OPERATIONAL'):
    App.g_kEventManager.AddBroadcastPythonFuncHandler(getattr(App, e), App.g_kTGActionManager, __name__ + '.Note')
# power
run(5)
ps = pA.GetPowerSubsystem()
print 'power: main %.0f backup %.0f, shields received %.2f pct %.2f' % (ps.GetMainBatteryPower(), ps.GetBackupBatteryPower(), pA.GetShields().GetPowerReceived(), pA.GetShields().GetPowerPercentage())
# repair: damage the sensors below the disabled level and let the repair teams fix them
sen = pA.GetSensorSubsystem()
sen.SetConditionPercentage(0.1)
run(1)
print 'repair: sensors %.2f disabled %d, being repaired %d' % (sen.GetConditionPercentage(), sen.IsDisabled(), pA.GetRepairSubsystem().IsBeingRepaired(sen))
run(120)
print 'repair: sensors after 120 s %.2f disabled %d; events disabled %d operational %d completed %d' % (sen.GetConditionPercentage(), sen.IsDisabled(),
    ev.count(App.ET_SUBSYSTEM_COMPLETELY_DISABLED), ev.count(App.ET_SUBSYSTEM_PARTIALLY_OPERATIONAL), ev.count(App.ET_REPAIR_COMPLETED))
# cloak: the Bird of Prey cloaks (5 s after the 1 s shield delay) and decloaks; the Galaxy loses it from its sensors
cl = pB.GetCloakingSubsystem()
cl.StartCloaking()
run(0.5)
print 'cloak: cloaking %d shields on %d' % (cl.IsCloaking(), pB.GetShields().IsOn())
run(6)
print 'cloak: cloaked %d ship cloaked %d visible to Galaxy %d' % (cl.IsCloaked(), pB.IsCloaked(), pA.GetSensorSubsystem().IsObjectVisible(pB))
cl.StopCloaking()
run(5.5)
print 'cloak: decloaking %d cloaked %d; after 1 s shield delay:' % (cl.IsDecloaking(), cl.IsCloaked()),
run(1.5)
print 'shields on %d visible %d' % (pB.GetShields().IsOn(), pA.GetSensorSubsystem().IsObjectVisible(pB))
# alert levels: green turns shields and weapons off, yellow turns the shields on, red everything (ET_SET_ALERT_LEVEL to the ship)
for lvl in (pA.GREEN_ALERT, pA.YELLOW_ALERT, pA.RED_ALERT):
    e = App.TGIntEvent_Create(); e.SetEventType(App.ET_SET_ALERT_LEVEL); e.SetDestination(pA); e.SetInt(lvl); App.g_kEventManager.AddEvent(e)
    run(0.2)
    print 'alert %d: level %d shields %d phasers %d torpedoes %d sensors %d' % (lvl, pA.GetAlertLevel(), pA.GetShields().IsOn(), pA.GetPhaserSystem().IsOn(), pA.GetTorpedoSystem().IsOn(), pA.GetSensorSubsystem().IsOn())
