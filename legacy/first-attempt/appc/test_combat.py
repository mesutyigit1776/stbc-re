# Combat test: a Galaxy fires its phasers at a Bird of Prey until it explodes.
import sys, os
import Appc
execfile(os.path.join(os.path.dirname(sys.argv[0]), 'host_boot.py'))
import App, loadspacehelper
pSet = App.SetClass_Create(); App.g_kSetManager.AddSet(pSet, 'CombatTest')
pA = loadspacehelper.CreateShip('Galaxy', pSet, 'Attacker', None, 0, 0)
pB = loadspacehelper.CreateShip('BirdOfPrey', pSet, 'Target', None, 0, 0)
p = App.TGPoint3(); p.SetXYZ(0.0, 15.0, 0.0); pB.SetTranslate(p)          # 15 units ahead of the Galaxy
class H:
    n = 0
def Exploding(pObject, pEvent):
    H.n = H.n + 1
    print '  ET_OBJECT_EXPLODING for', App.ShipClass_Cast(pEvent.GetDestination()).GetName()
    pObject.CallNextHandler(pEvent)
App.g_kEventManager.AddBroadcastPythonFuncHandler(App.ET_OBJECT_EXPLODING, App.g_kTGActionManager, __name__ + '.Exploding')
hits = []
def Hit(pObject, pEvent):
    hits.append(1)
    pObject.CallNextHandler(pEvent)
App.g_kEventManager.AddBroadcastPythonFuncHandler(App.ET_WEAPON_HIT, App.g_kTGActionManager, __name__ + '.Hit')
pPh = pA.GetPhaserSystem()
print 'phaser banks', pPh.GetNumChildSubsystems(), 'target shields %.0f' % pB.GetShields().GetCurShields(0), 'hull %.0f' % pB.GetHull().GetCondition()
sh = pB.GetShields(); print 'shield on', sh.IsOn(), 'disabled', sh.IsDisabled(), 'hurtable', pB.IsHurtable(), 'cond', sh.GetConditionPercentage()
pPh.StartFiring(pB, None)
t = 0.0
for i in range(6000):
    t = t + 0.1
    Appc.engine_tick(t, t)
    if i % 500 == 0 or pB.IsDying():
        n = 0
        for k in range(pPh.GetNumChildSubsystems()):
            if App.Weapon_Cast(pPh.GetChildSubsystem(k)).IsFiring(): n = n + 1
        s = pB.GetShields(); sv = map(lambda k, s=s: int(s.GetCurShields(k)), range(6))
        print 't=%4.1f firing %d shields %s hull %6.1f charge0 %5.2f hits %d' % (t, n, sv, pB.GetHull().GetCondition(), App.EnergyWeapon_Cast(pPh.GetChildSubsystem(0)).GetChargeLevel(), len(hits))
    if pB.IsDying(): break
print 'target dying:', pB.IsDying(), 'explosion events:', H.n

# pulse weapons: a Bird of Prey fires its disruptor cannons (projectiles) at a Galaxy ahead of it
fired = []
def Fired(pObject, pEvent):
    fired.append(1)
    pObject.CallNextHandler(pEvent)
App.g_kEventManager.AddBroadcastPythonFuncHandler(App.ET_WEAPON_FIRED, App.g_kTGActionManager, __name__ + '.Fired')
pC = loadspacehelper.CreateShip('BirdOfPrey', pSet, 'Raider', None, 0, 0)
pD = loadspacehelper.CreateShip('Galaxy', pSet, 'Victim', None, 0, 0)
p = App.TGPoint3(); p.SetXYZ(100.0, 0.0, 0.0); pC.SetTranslate(p)
p = App.TGPoint3(); p.SetXYZ(100.0, 20.0, 0.0); pD.SetTranslate(p)
pPulse = pC.GetPulseWeaponSystem()
print 'pulse weapons', pPulse and pPulse.GetNumChildSubsystems()
del hits[:]
pPulse.StartFiring(pD, None)
for i in range(300):
    t = t + 0.1
    Appc.engine_tick(t, t)
s = pD.GetShields(); sv = map(lambda k, s=s: int(s.GetCurShields(k)), range(6))
print 'after 30 s: shots fired %d, hits %d, victim shields %s hull %.1f' % (len(fired), len(hits), sv, pD.GetHull().GetCondition())
