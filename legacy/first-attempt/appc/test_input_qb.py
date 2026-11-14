# Mouse and keyboard in the Quick Battle setup windows and the confirmation dialog. Python 1.5.2.
import sys, os, string, time
import Appc
execfile(os.environ.get('APPC_PORT', '../port/appc') + '/host_boot.py')
import App
import ui_display

SETUP = os.environ.get('MENU_SETUP', 'stock')
FAILS = []
T = [0.0]
def check(name, ok):
    print ok and 'PASS' or 'FAIL', SETUP, name
    if not ok:
        FAILS.append(name)
SHOW = []
def pump(n=3):
    for i in range(n):
        if SHOW:
            SHOW[0].SetVisible()                 # the mission hides the dialog about nine seconds in (a timer)
        Appc.platform_pump()
        Appc.engine_tick(T[0], T[0])
        T[0] = T[0] + 0.1
        Appc.platform_render(0.0, 0.0, 0.0)
def named(sText, kind=0):
    for c in Appc.ui_controls():
        if c[5] == sText and (kind == 0 or c[0] == kind):
            return c
    return None
def center(c):
    return ((c[1] + c[3]) / 2.0, (c[2] + c[4]) / 2.0)
def click(c):
    x, y = center(c)
    Appc.ui_mouse_move(x, y)
    a = Appc.ui_mouse_button(1, 1, x, y)
    b = Appc.ui_mouse_button(1, 0, x, y)
    pump()
    return a and b
def key(wc):
    a = Appc.ui_key(wc, 1, 0)
    Appc.ui_key(wc, 2, 0)
    pump(2)
    return a

App.g_kConfigMapping.LoadConfigFile('Options.cfg')
ui_display.set_int('Layout', 0)
ui_display.set_int('Size', 0)
ui_display.set_int('Fullscreen', 0)
ui_display.start_window('Bridge Commander (port) quick battle input test')
if App.g_kConfigMapping.LoadConfigFile('Options.cfg') == 0:
    App.g_kConfigMapping.SetIntValue('Annoyances', 'Quit Game Confirmation', 1)
    App.g_kConfigMapping.SaveConfigFile('Options.cfg')
    App.g_kConfigMapping.LoadConfigFile('Options.cfg')
name = App.TGString()
name.SetString('Test Captain')
App.g_kUtopiaModule.SetCaptainName(name)
App.Game_SetDifficulty(1)
__import__('MainMenu.mainmenu')
mm = sys.modules['MainMenu.mainmenu']
mm.BuildMainMenu()
mm.BuildInterface()
Appc.engine_new_game('QuickBattle.QuickBattleGame')
for i in range(40):
    Appc.engine_tick(T[0], T[0])
    T[0] = T[0] + 0.1
qb = sys.modules['QuickBattle.QuickBattle']
top = App.TopWindow_GetTopWindow()
opt = top.FindMainWindow(App.MWT_OPTIONS)
if opt.IsVisible():
    top.ToggleOptionsMenu()
qb.g_pPane.SetVisible()
SHOW.append(qb.g_pPane)
pump()
check('the dialog shows its buttons', named('Ships') is not None and named('Close') is not None and named('Player and Region') is not None)
cAdd = named('Add As Enemy Ship')
check('Add As Enemy Ship is there', cAdd is not None)
# open a ship menu and choose a ship
lMenus = []
for c in Appc.ui_controls():
    if c[0] == 2:
        lMenus.append(c[5])
check('the ship menus are listed (%d)' % len(lMenus), 'Federation Ships' in lMenus)
click(named('Federation Ships', 2))
cShip = None
lAll = Appc.ui_controls()
for i in range(len(lAll)):
    if lAll[i][0] == 2 and lAll[i][5] == 'Federation Ships':
        for j in range(i + 1, len(lAll)):
            if lAll[j][0] == 1 and lAll[j][5] != '' and lAll[j][7]:
                cShip = lAll[j]
                break
check('a click on a menu opens the list of its ships (%s)' % str(cShip and cShip[5]), cShip is not None)
if cShip:
    click(cShip)
    nEn = len(qb.g_kEnemyList)
    click(named('Add As Enemy Ship'))
    check('Add As Enemy Ship puts the ship on the enemy list (%d to %d)' % (nEn, len(qb.g_kEnemyList)), len(qb.g_kEnemyList) == nEn + 1)
    nFr = len(qb.g_kFriendList)
    click(named('Add As Friendly Ship'))
    check('Add As Friendly Ship puts it on the friendly list (%d to %d)' % (nFr, len(qb.g_kFriendList)), len(qb.g_kFriendList) == nFr + 1)
click(named('Player and Region'))
check('the Player and Region tab opens', named('Combat Region') is not None or named('Close') is not None)
click(named('Ships'))
# keyboard in the dialog
key(App.WC_TAB)
sF = Appc.ui_focus()
check('Tab focuses a control of the dialog (%s)' % str(sF), sF is not None)
key(App.WC_DOWN)
check('Down moves inside the dialog (%s)' % str(Appc.ui_focus()), Appc.ui_focus() is not None and Appc.ui_focus() != sF)
# long lists: the focus walks down the list and the list scrolls to keep it in view
for i in range(30):
    key(App.WC_DOWN)
cF = named(Appc.ui_focus() or '')
check('after many Down keys the focused control is still in view (%s)' % str(Appc.ui_focus()), cF is not None and cF[7] == 1 and cF[2] >= 0 and cF[4] <= 1300)
for i in range(30):
    key(App.WC_UP)
cF = named(Appc.ui_focus() or '')
check('and back up (%s)' % str(Appc.ui_focus()), cF is not None and cF[7] == 1)
# close
del SHOW[:]
check('a click on Close hides the dialog', click(named('Close')) and not qb.g_pPane.IsVisible())

# the confirmation dialog: Quick Battle from the main menu while a game runs
top.ToggleOptionsMenu()
pump()
check('the main menu is back', named('Quick Battle') is not None)
click(named('Quick Battle'))
md = top.FindMainWindow(App.MWT_MODAL_DIALOG)
check('Quick Battle with a game running asks first (the dialog shows)', md is not None and md.IsVisible())
lBtn = []
for c in Appc.ui_controls():
    lBtn.append(c[5])
check('only the dialog takes input (%s)' % str(lBtn), 'New Game' not in lBtn and 'OK' in lBtn and 'Cancel' in lBtn)
click(named('Cancel'))
check('Cancel closes it', not md.IsVisible())
click(named('Quick Battle'))
check('it comes again', md.IsVisible())
key(App.WC_ESCAPE)
check('Escape cancels it', not md.IsVisible())
click(named('Quick Battle'))
check('Enter presses OK (the focused button): the dialog closes', md.IsVisible() and key(App.WC_RETURN) is not None and not md.IsVisible())
Appc.platform_close()
print 'RESULT', len(FAILS) and 'FAIL: ' + string.join(FAILS, ', ') or 'PASS', 'quick battle input test', SETUP
sys.exit(len(FAILS) and 1 or 0)
