# Motion test: a stock ship accelerates to its impulse speed and turns at a set angular velocity.
import sys, os
import Appc
execfile(os.path.join(os.path.dirname(sys.argv[0]), 'host_boot.py'))
import App, loadspacehelper
pSet = App.SetClass_Create(); App.g_kSetManager.AddSet(pSet, 'MotionTest')
pShip = loadspacehelper.CreateShip('Galaxy', pSet, 'TestGalaxy', None, 0, 0)
pImp = pShip.GetImpulseEngineSubsystem()
pProp = App.ImpulseEngineProperty_Cast(pImp.GetProperty())
print 'max speed', pProp.GetMaxSpeed(), 'max accel', pProp.GetMaxAccel(), 'max angular accel', pProp.GetMaxAngularAccel()
pShip.SetImpulse(1.0, App.TGPoint3_GetModelForward(), 0)
t = 0.0
for i in range(100):
    t = t + 0.1
    Appc.engine_tick(t, t)
    if i % 20 == 19:
        v = pShip.GetVelocityTG(); p = pShip.GetTranslate()
        print 't=%.1f speed %.3f pos (%.2f %.2f %.2f)' % (t, v.Length(), p.GetX(), p.GetY(), p.GetZ())
w = App.TGPoint3(); w.SetXYZ(0.0, 0.0, 0.5)
pShip.SetTargetAngularVelocityDirect(w)
for i in range(30):
    t = t + 0.1
    Appc.engine_tick(t, t)
f = pShip.GetWorldForwardTG()
print 'after turning: angular speed %.3f forward (%.3f %.3f %.3f)' % (pShip.GetAngularVelocityTG().Length(), f.GetX(), f.GetY(), f.GetZ())

# 1. turn toward a point behind and to the side: forward converges to the direction
pShip.SetImpulse(0.0, App.TGPoint3_GetModelForward(), 0)
w.SetXYZ(0.0, 0.0, 0.0); pShip.SetTargetAngularVelocityDirect(w)
p = pShip.GetTranslate(); goal = App.TGPoint3(); goal.SetXYZ(p.GetX() + 100.0, p.GetY() - 100.0, p.GetZ() + 30.0)
for i in range(400):
    t = t + 0.1
    pShip.TurnTowardLocation(goal)
    Appc.engine_tick(t, t)
f = pShip.GetWorldForwardTG(); p = pShip.GetTranslate()
d = App.TGPoint3(); d.SetXYZ(goal.GetX() - p.GetX(), goal.GetY() - p.GetY(), goal.GetZ() - p.GetZ()); d.Unitize()
print 'turn toward: dot(forward, goal) %.3f after %.0f s, angular speed %.3f' % (f.Dot(d), 40.0, pShip.GetAngularVelocityTG().Length())

# 2. engine damage and power lower the top speed
pProp = App.ImpulseEngineProperty_Cast(pImp.GetProperty())
print 'max speed: full %.3f' % pImp.GetMaxSpeed(),
for k in range(pImp.GetNumChildSubsystems()):
    c = pImp.GetChildSubsystem(k); c.SetConditionPercentage(0.5)
print ' engines at 50%% %.3f' % pImp.GetMaxSpeed(),
pImp.SetPowerPercentageWanted(0.5)
print ' + power 50%% %.3f' % pImp.GetMaxSpeed(),
pImp.TurnOff()
print ' off %.3f' % pImp.GetMaxSpeed()
pImp.TurnOn(); pImp.SetPowerPercentageWanted(1.0)
for k in range(pImp.GetNumChildSubsystems()): pImp.GetChildSubsystem(k).SetConditionPercentage(1.0)

# 3. in-system warp to a far object
pFar = loadspacehelper.CreateShip('FedStarbase', pSet, 'Far', None, 0, 0)
fp = pShip.GetTranslate(); pos = App.TGPoint3(); pos.SetXYZ(fp.GetX() + 2000.0, fp.GetY(), fp.GetZ()); pFar.SetTranslate(pos)
arrived = 0
for i in range(600):
    t = t + 0.1
    if not arrived and (not pShip.IsDoingInSystemWarp() or i == 0):
        if not pShip.InSystemWarp(pFar, 20.0):          # 0: close enough (the AI then stops asking and steadies the ship)
            arrived = 1; w.SetXYZ(0.0, 0.0, 0.0); pShip.SetTargetAngularVelocityDirect(w)
    Appc.engine_tick(t, t)
    if i % 150 == 149:
        p = pShip.GetTranslate()
        print 'warp t=%.0f warping %d speed %.1f distance %.1f' % (i * 0.1, pShip.IsDoingInSystemWarp(), pShip.GetVelocityTG().Length(), ((p.GetX() - pos.GetX()) ** 2 + (p.GetY() - pos.GetY()) ** 2) ** 0.5)

# 4. set change (what the warp sequence scripts do): the ship keeps its motion in the new set
pSet2 = App.SetClass_Create(); App.g_kSetManager.AddSet(pSet2, 'MotionTest2')
w.SetXYZ(0.0, 0.0, 0.0); pShip.SetTargetAngularVelocityDirect(w)       # stop the turn the warp left
pShip.SetImpulse(1.0, App.TGPoint3_GetModelForward(), 0)
for i in range(30):
    t = t + 0.1; Appc.engine_tick(t, t)
v0 = pShip.GetVelocityTG().Length()
pSet.RemoveObjectFromSet('TestGalaxy'); pSet2.AddObjectToSet(pShip, 'TestGalaxy')
for i in range(30):
    t = t + 0.1; Appc.engine_tick(t, t)
print 'set change: in %s, speed before %.2f after %.2f, found by name %d' % (pShip.GetContainingSet().GetName(), v0, pShip.GetVelocityTG().Length(), pSet2.GetObject('TestGalaxy') != None)
