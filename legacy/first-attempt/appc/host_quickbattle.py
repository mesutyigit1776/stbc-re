# Headless Quick Battle. The XO's Start Simulation button is pressed the way the UI does it
# (ET_START_SIMULATION to the XO), then the battle runs in game time until a ship is destroyed.
# Usage (from game/): ../port/appc/run.sh ../port/appc/host_quickbattle.py [enemy_ship_type] [ai_level]
#   enemy_ship_type: Galaxy (default), BirdOfPrey (cloaks: the player cannot target it while it is cloaked). ai_level: 0.0 low,
#   0.5 medium, 1.0 high. A third argument 'weak' starts the enemy at 5% hull and no shields (a test of the win path).
# QB_START_DEST=menu sends the start event to the XO's menu (Orion Delta). QB_PLAYER sets the player's ship type.
# Window keys: Space or Return starts the battle (QB_AUTOSTART=1 starts it at once), C switches the camera between the
# Target and Chase modes (TacticalInterfaceHandlers.CycleCameraMode), Escape closes the window.
# QB_DEBUG=1 prints the state of both ships every 100 s of game time. Prints PASS/FAIL lines; exit code 1 on FAIL.
# port/appc/test_quickbattle.sh runs the scenarios.
import sys, os, string, math
import Appc
execfile(os.environ.get('APPC_PORT', '../port/appc') + '/host_boot.py')     # same start-up as the boot script
import App

ENEMY = 'Galaxy'
if len(sys.argv) > 1:
    ENEMY = sys.argv[1]
PLAYER = os.environ.get('QB_PLAYER', 'Galaxy')     # the player's ship type (a module under scripts/ships)
AI_LEVEL = 0.5
if len(sys.argv) > 2:
    AI_LEVEL = float(sys.argv[2])
DEBUG = os.environ.has_key('QB_DEBUG')
WEAK = len(sys.argv) > 3 and sys.argv[3] == 'weak'
LIMIT_TICKS = 24000                       # 2400 s of game time at 0.1 s per tick
# QB_WINDOW=1 runs the battle in a window: one frame per loop pass, game time follows the clock (at most 0.1 s a
# frame), and the run stops after QB_WINDOW_SECONDS of real time (default 60) or when the window is closed.
WINDOW = os.environ.has_key('QB_WINDOW')
WINDOW_SECONDS = float(os.environ.get('QB_WINDOW_SECONDS', '60'))
CAMERA_BACK = os.environ.get('QB_CAMERA_BACK')     # QB_CAMERA_BACK=n: the host places the camera n units behind the player
AUTOSTART = os.environ.has_key('QB_AUTOSTART') or not WINDOW
FX_MAX = [0, 0, 0]                        # most beams, projectiles and explosions drawn in one frame
FRAMES = [0]
DRAWN = [0, 0]                            # objects and triangles drawn in the last frame
DRAWN_TOTAL = [0]                         # triangles drawn over the run
CLOCK = [0.0, 0.0, 0.0]                   # start, last frame, end of the window run
STOP = [0]
if WINDOW:
    import ui_display
    App.g_kConfigMapping.LoadConfigFile('Options.cfg')       # the window size and the interface settings
    ui_display.start_window('Bridge Commander (port)')

KEYS = []                                 # key presses not handled yet
SNAP_AT = []
SNAP_DONE = []
if os.environ.has_key('QB_SNAP_AT'):
    SNAP_AT = map(float, string.split(os.environ['QB_SNAP_AT'], ','))
FAILS = []
def check(name, ok):
    print ok and 'PASS' or 'FAIL', name
    if not ok:
        FAILS.append(name)

def dist(a, b):
    return math.sqrt((a.GetX() - b.GetX()) ** 2 + (a.GetY() - b.GetY()) ** 2 + (a.GetZ() - b.GetZ()) ** 2)

LAST = [None]
def ships():
    pE = None
    for kid in qb.g_kShips.keys():
        pE = App.ShipClass_Cast(App.TGObject_GetTGObjectPtr(kid))
    return pE, App.Game_GetCurrentGame().GetPlayer()

MOVE0 = [None]                             # the player's position when the battle started, for the move check
def place_camera():
    if not globals().has_key('qb') or not globals().has_key('pMission'):
        return
    # Fixed host view, only with QB_CAMERA_BACK: the camera's modes place the camera (tg_camera_tick).
    pRS = App.g_kSetManager.GetRenderedSet()
    pCam = pRS and pRS.GetActiveCamera()
    pE, pP = ships()
    if not (pCam and pE and pP):
        return
    a = pP.GetWorldLocation()
    b = pE.GetWorldLocation()
    dx, dy, dz = b.GetX() - a.GetX(), b.GetY() - a.GetY(), b.GetZ() - a.GetZ()
    d = math.sqrt(dx * dx + dy * dy + dz * dz) or 1.0
    mx, my, mz = a.GetX(), a.GetY(), a.GetZ()                      # on the player's ship, from behind it, toward the enemy
    back = float(CAMERA_BACK)
    cx, cy, cz = mx - dx / d * back, my - dy / d * back, mz - dz / d * back + back * 0.35
    fx, fy, fz = mx - cx, my - cy, mz - cz
    fl = math.sqrt(fx * fx + fy * fy + fz * fz) or 1.0
    pos = App.TGPoint3()
    pos.SetXYZ(cx, cy, cz)
    fwd = App.TGPoint3()
    fwd.SetXYZ(fx / fl, fy / fl, fz / fl)
    up = App.TGPoint3()
    up.SetXYZ(0.0, 0.0, 1.0)
    pCam.SetTranslate(pos)
    pCam.AlignToVectors(fwd, up)
    pCam.UpdateNodeOnly()

def tick(t):
    if WINDOW:
        if not Appc.platform_pump():
            STOP[0] = 1
        ui_display.service()                  # the window changed size: fit the UI again
        now = Appc.platform_ticks()
        if CLOCK[1] == 0.0:
            CLOCK[0] = now
        dt = 0.1
        if CLOCK[1]:
            dt = min(now - CLOCK[1], 0.1)
        CLOCK[1] = now
        FRAMES[0] = FRAMES[0] + 1
        if now - CLOCK[0] >= WINDOW_SECONDS:
            STOP[0] = 1
        t = t + dt - 0.1
    Appc.engine_tick(t, t)
    if WINDOW:
        if MOVE0[0] is None and globals().has_key('qb') and qb.bInSimulation:
            MOVE0[0] = App.Game_GetCurrentGame().GetPlayer().GetWorldLocation()
        if CAMERA_BACK:
            place_camera()
        for key in Appc.platform_keys():
            KEYS.append(key)
        objs, tris = Appc.platform_render(0.0, 0.0, 0.0)
        fx = Appc.platform_fx()
        for k in range(3):
            FX_MAX[k] = max(FX_MAX[k], fx[k])
        if SNAP_AT and globals().has_key('t0') and t - t0 >= SNAP_AT[0]:      # QB_SNAP_AT=t1,t2,..: a snapshot when the battle's game time passes each value (QB_SNAPSHOT is the file stem)
            print 'SNAP', SNAP_AT[0], Appc.platform_snapshot('%s_%d.raw' % (os.environ.get('QB_SNAPSHOT', 'snap'), len(SNAP_DONE)))
            SNAP_DONE.append(SNAP_AT[0]); del SNAP_AT[0]
        if os.environ.has_key('QB_SNAP_SHOTS') and fx[1] > 0 and len(SNAP_DONE) < int(os.environ['QB_SNAP_SHOTS']) and (not SNAP_DONE or t - SNAP_DONE[-1] > 0.4):     # QB_SNAP_SHOTS=n: up to n snapshots while a projectile is drawn
            print 'SNAP shots', t, Appc.platform_snapshot('%s_%d.raw' % (os.environ.get('QB_SNAPSHOT', 'snap'), len(SNAP_DONE)))
            SNAP_DONE.append(t)
        DRAWN[0] = objs
        DRAWN[1] = tris
        DRAWN_TOTAL[0] = DRAWN_TOTAL[0] + tris
    if DEBUG:
        pD = App.Game_GetCurrentGame().GetPlayer()
        cur = pD and pD.GetContainingSet() and 'set' or 'NONE'
        if cur != LAST[0]:
            print 'DBGSET t=%.1f player=%s set=%s dead=%d dying=%d' % (t, pD and pD.GetName() or None, cur, pD and pD.IsDead() or 0, pD and pD.IsDying() or 0)
            LAST[0] = cur
    return t + 0.1
def stopped():
    return STOP[0] == 1

name = App.TGString()
name.SetString('Test Captain')
App.g_kUtopiaModule.SetCaptainName(name)
App.Game_SetDifficulty(1)
# A fresh install has no Options.cfg, and the Quick Battle dialog is only built when it can be read. Write the file the
# options menu would write (one value), then read it back.
if App.g_kConfigMapping.LoadConfigFile('Options.cfg') == 0:
    App.g_kConfigMapping.SetIntValue('Annoyances', 'Quit Game Confirmation', 1)
    App.g_kConfigMapping.SaveConfigFile('Options.cfg')
    App.g_kConfigMapping.LoadConfigFile('Options.cfg')

# The main menu is built before any game starts (the original shows it after the opening movies). Mod Quick Battles read its
# buttons (UpdateColors), so build it the same way here.
__import__('MainMenu.mainmenu')
sys.modules['MainMenu.mainmenu'].BuildInterface()

print '--- engine_new_game QuickBattle'
game = Appc.engine_new_game('QuickBattle.QuickBattleGame')
t = 0.0
for i in range(60):
    t = tick(t)
qb = sys.modules['QuickBattle.QuickBattle']
pMission = App.Game_GetCurrentGame().GetCurrentEpisode().GetCurrentMission()
check('mission loaded', pMission is not None and pMission.GetScript() == 'QuickBattle.QuickBattle')
pPlayer = App.Game_GetCurrentGame().GetPlayer()
check('player ship exists', pPlayer is not None and pPlayer.GetName() == 'Player' and App.ShipClass_Cast(pPlayer) is not None)

# The enemy list, as the Add Enemy button builds it: [type, TGL name, destroyed line, AI script, side, AI level].
# (type, TGL name, destroyed line) as in QuickBattle.g_dEnemyShipTypeToDetails.
qb.g_kFriendList = []
ENEMY_TABLE = {'Galaxy': ('Galaxy', 'QBEnemyGalaxyDestroyed'), 'BirdOfPrey': ('BOP', 'QBEnemyBOPDestroyed')}
kName, kLine = ENEMY_TABLE.get(ENEMY, (ENEMY, 'QBEnemyGenericShipDestroyed'))
qb.g_kEnemyList = [[ENEMY, kName, kLine, 'QuickBattleAI', 'Enemy', AI_LEVEL]]

qb.g_sPlayerType = PLAYER
if os.environ.has_key('QB_FOUNDATION'):
    # Remastered builds its lists from Foundation ship definitions: [ship, AI level, destroyed line, AI script, side]
    import Foundation
    def find_ship(f):
        for ship in Foundation.shipList._keyList.values():
            if ship.shipFile == f:
                return ship
        return None
    kEnemy = find_ship(ENEMY)
    qb.g_kEnemyList = [[kEnemy, AI_LEVEL, kEnemy.StrEnemyDestroyed(), kEnemy.StrEnemyAI(), 'Enemy']]
    qb.g_sPlayerType = find_ship(PLAYER)
if not AUTOSTART:
    # keyboard start: the setup screen is not drawn yet, so Space or Return starts the set-up battle
    print 'Press Space or Return in the window to start the battle (Escape closes it).'
    while not stopped():
        t = tick(t)
        if 'Space' in KEYS or 'Return' in KEYS:
            break
    del KEYS[:]
# The Start button sends the event to the XO (stock) or to the XO's menu (Orion Delta registers its handlers there).
pEvent = App.TGEvent_Create()
pEvent.SetEventType(qb.ET_START_SIMULATION)
pEvent.SetDestination(os.environ.get('QB_START_DEST') == 'menu' and qb.g_pXOMenu or qb.g_pXO)
App.g_kEventManager.AddEvent(pEvent)
# Wait for the start by game time, not by a number of ticks: in the window a tick is one frame of real time, and frames are not
# held to vsync while the window is hidden (screen locked, other space), so 200 ticks can pass in almost no game time.
t1 = t
while t - t1 < 20.0:
    t = tick(t)
    if qb.bInSimulation or stopped():
        break
check('simulation started', qb.bInSimulation == 1)
if os.environ.has_key('QB_UIDUMP'):                       # prints the UI tree once the battle runs and the opening scene is over (debugging the tactical windows)
    for i in range(20):
        t = tick(t)
    t1 = t
    while App.TopWindow_GetTopWindow().IsCutsceneMode() and t - t1 < 100:
        t = tick(t)
    Appc.ui_dump(int(os.environ['QB_UIDUMP']))
if os.environ.has_key('QB_PY'):                            # runs a debug script once the battle runs and the opening scene is over (it may use t and tick)
    for i in range(20):
        t = tick(t)
    t1 = t
    while App.TopWindow_GetTopWindow().IsCutsceneMode() and t - t1 < 100:
        t = tick(t)
    execfile(os.environ['QB_PY'])
check('one enemy tracked', len(qb.g_kShips) == 1)
check('win/lose state set up', qb.bWonOrLost == 0)
# Keyboard path: key presses go through the key bindings to the tactical handlers (steering W, fire F), as the
# window does. QB_INPUT=1 checks it here; the battle below still runs on the direct calls.
if os.environ.has_key('QB_INPUT'):
    pK = App.Game_GetCurrentGame().GetPlayer()
    Appc.engine_input_key(App.WC_W, App.TGKeyboardEvent.KS_KEYDOWN)
    for i in range(10):
        t = tick(t)
    check('W key turns the player ship (angular velocity %.3f)' % pK.GetAngularVelocityTG().Length(), pK.GetAngularVelocityTG().Length() > 0.01)
    Appc.engine_input_key(App.WC_W, App.TGKeyboardEvent.KS_KEYUP)
    pK.SetTarget(qb.pEnemies.GetNameTuple()[0])          # the target is picked as the player does (the target keys)
    Appc.engine_input_key(App.WC_F, App.TGKeyboardEvent.KS_KEYDOWN)
    t = tick(t)
    check('F key fires the primary weapons', pK.GetPhaserSystem().IsFiring() == 1)
    Appc.engine_input_key(App.WC_F, App.TGKeyboardEvent.KS_KEYUP)
    t = tick(t)
    check('F key up stops the primary weapons', pK.GetPhaserSystem().IsFiring() == 0)

if WEAK:
    pWeak = App.ShipClass_Cast(App.TGObject_GetTGObjectPtr(qb.g_kShips.keys()[0]))
    pWeak.GetHull().SetCondition(pWeak.GetHull().GetMaxCondition() * 0.05)
    for iFacing in range(6):
        pWeak.GetShields().SetCurShields(iFacing, 0.0)
    print 'weak enemy hull', pWeak.GetHull().GetCondition(), 'shields', pWeak.GetShields().GetCurShields(0)
# No human plays here: the host acts as the player (as host_e1m1 does). The player picks the enemy in the target list
# (ShipClass.SetTarget), flies at it and holds the fire button (see the loop below). The enemy has its AI.
pPlayer = App.Game_GetCurrentGame().GetPlayer()
pEnemyName = qb.pEnemies.GetNameTuple()[0]
if os.environ.has_key('QB_PLAYER_AI'):
    # the player's ship flies on the stock friendly AI instead of the host (checks the AI attack path)
    __import__('QuickBattle.QuickBattleFriendlyAI')
    pPlayer.SetAI(sys.modules['QuickBattle.QuickBattleFriendlyAI'].CreateAI(pPlayer), 0, 0)
else:
    pPlayer.SetTarget(pEnemyName)
    pPlayer.SetAlertLevel(pPlayer.RED_ALERT)     # the stock Quick Battle starts at red alert, Remastered and Orion Delta at green: a human raises it first
if not os.environ.has_key('QB_PLAYER_AI'):
    pPlayer.GetPhaserSystem().StartFiring(App.ShipClass_Cast(qb.g_pSet.GetObject(pEnemyName)), None)

# Run until a ship is destroyed. The enemy's death ends the battle as a win (ShipDestroyed); the player's death ends it as a
# loss (ShipDestroyed starts a one-second timer for ET_END_SIMULATION, which runs the lose sequence).
enemy_dead = 0
player_dead = 0
t0 = t
for i in range(LIMIT_TICKS):
    t = tick(t)
    if stopped():
        break
    if len(qb.g_kShips) == 0:
        enemy_dead = 1
        break
    pP = App.Game_GetCurrentGame().GetPlayer()
    if pP and len(qb.g_kShips) and not os.environ.has_key('QB_PLAYER_AI'):          # steer toward the enemy and go at full impulse, as a pilot would
        pE, pX = ships()
        if pE:
            pP.TurnTowardLocation(pE.GetWorldLocation())
            pP.SetImpulse(1.0, App.TGPoint3_GetModelForward(), 0)
            pP.GetPhaserSystem().StartFiring(pE, None)               # hold the fire button on the target
            if pP.GetTorpedoSystem():
                pP.GetTorpedoSystem().StartFiring(pE, None)          # and the torpedo button
    if 'C' in KEYS:                                                  # camera: Target <-> Chase
        __import__('TacticalInterfaceHandlers')
        sys.modules['TacticalInterfaceHandlers'].CycleCameraMode(None, None)
    del KEYS[:]
    if pP is None or pP.IsDying() or pP.IsDead():
        player_dead = 1
        break
    if DEBUG and i % int(os.environ.get('QB_DEBUG_EVERY', '1000')) == 0:
        pE, pP = ships()
        if pE and pP:
            pPh = pP.GetPhaserSystem()
            print 'DBG t=%6.1f dist=%.0f enemy hull=%.0f shields=%.0f speed=%.1f  player hull=%.0f speed=%.1f phasers firing=%d torps=%d target=%s' % (t, dist(pE.GetWorldLocation(), pP.GetWorldLocation()), pE.GetHull().GetCondition(), pE.GetShields().GetCurShields(0), pE.GetVelocityTG().Length(), pP.GetHull().GetCondition(), pP.GetVelocityTG().Length(), pPh.IsFiring(), pP.GetTorpedoSystem() and pP.GetTorpedoSystem().IsFiring(), pP.GetTarget() and pP.GetTarget().GetName())
if WINDOW and not (enemy_dead or player_dead):
    # window run cut short by the clock or a closed window: report the frame rate instead of the battle result
    elapsed = Appc.platform_ticks() - CLOCK[0]
    fps = FRAMES[0] / max(elapsed, 0.001)
    print 'WINDOW frames %d in %.1f s real time: %.1f fps, game time %.1f s, ships tracked %d, last frame %d objects %d triangles' % (FRAMES[0], elapsed, fps, t - t0, len(qb.g_kShips), DRAWN[0], DRAWN[1])
    check('ships are drawn (objects in the last frame)', DRAWN[0] >= 2)
    pMv = App.Game_GetCurrentGame().GetPlayer()
    mvd = 0.0
    if MOVE0[0] is not None:
        mvd = dist(pMv.GetWorldLocation(), MOVE0[0])
    check('ships move in the window (player moved %.1f units)' % mvd, mvd > 1.0)
    check('window runs at frame rate (40 fps or more)', fps >= 40.0)
    check('battle advanced in the window', t - t0 > 1.0)
    print 'EFFECTS most in one frame: beams %d, projectiles %d, explosions %d' % (FX_MAX[0], FX_MAX[1], FX_MAX[2])
    pCamM = App.Game_GetCurrentGame().GetPlayerCamera()
    pMode = pCamM and pCamM.GetCurrentCameraMode()
    print 'CAMERA mode', pMode and pMode.GetName()
    check('a camera mode places the camera', CAMERA_BACK is not None or pMode is not None)
    if os.environ.has_key('QB_SNAPSHOT'):
        pRS = App.g_kSetManager.GetRenderedSet()
        pCam = pRS and pRS.GetActiveCamera()
        pPl = App.Game_GetCurrentGame().GetPlayer()
        pCL = pCam and pCam.GetWorldLocation()
        pPL = pPl.GetWorldLocation()
        print 'SCENE set', pRS and pRS.GetName(), 'cam', pCam and pCam.GetName(), pCL and (pCL.GetX(), pCL.GetY(), pCL.GetZ()), 'player', (pPL.GetX(), pPL.GetY(), pPL.GetZ()), 'radius', pPl.GetRadius()
        print 'SNAPSHOT', Appc.platform_snapshot(os.environ['QB_SNAPSHOT'])
    Appc.platform_close()
    print 'RESULT', len(FAILS) and 'FAIL: ' + string.join(FAILS, ', ') or 'PASS', 'window run'
    sys.exit(len(FAILS) and 1 or 0)
check('a ship was destroyed', enemy_dead == 1 or player_dead == 1)
if enemy_dead:
    check('enemy destroyed: battle ends as a win', qb.bWonOrLost == 1 and qb.g_iNumEnemies == 0)
if player_dead:
    t2 = t
    while t - t2 < 120.0:                    # the wreck goes first (ET_OBJECT_DESTROYED), then the one-second timer (by game time: a
        t = tick(t)                          # window tick is one frame)
        if qb.bInSimulation == 0 or stopped():
            break
    check('player destroyed: battle ends as a loss (simulation stopped)', qb.bInSimulation == 0)
if WINDOW:
    print 'EFFECTS most in one frame: beams %d, projectiles %d, explosions %d' % (FX_MAX[0], FX_MAX[1], FX_MAX[2])
    Appc.platform_close()
print 'RESULT', len(FAILS) and 'FAIL: ' + string.join(FAILS, ', ') or 'PASS', 'after %.1f s, %s destroyed' % (t - t0, enemy_dead and 'enemy' or 'player')
sys.exit(len(FAILS) and 1 or 0)
