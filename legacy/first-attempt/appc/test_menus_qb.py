# Quick Battle setup windows in the shapes of test_menus.py: the dialog is built by the stock script when a Quick Battle
# starts, shown, and built again for each screen shape (what ui_display does when the window changes). Python 1.5.2.
import sys, os, string, time
import Appc
execfile(os.environ.get('APPC_PORT', '../port/appc') + '/host_boot.py')
import App
import ui_display

OUT = os.environ['MENU_OUT']
SETUP = os.environ.get('MENU_SETUP', 'stock')
FAILS = []
def check(name, ok):
    print ok and 'PASS' or 'FAIL', SETUP, name
    if not ok:
        FAILS.append(name)

App.g_kConfigMapping.LoadConfigFile('Options.cfg')
ui_display.start_window('Bridge Commander (port) menu test')
if App.g_kConfigMapping.LoadConfigFile('Options.cfg') == 0:
    App.g_kConfigMapping.SetIntValue('Annoyances', 'Quit Game Confirmation', 1)
    App.g_kConfigMapping.SaveConfigFile('Options.cfg')
    App.g_kConfigMapping.LoadConfigFile('Options.cfg')
name = App.TGString()
name.SetString('Test Captain')
App.g_kUtopiaModule.SetCaptainName(name)
App.Game_SetDifficulty(1)
__import__('MainMenu.mainmenu')
sys.modules['MainMenu.mainmenu'].BuildInterface()
Appc.engine_new_game('QuickBattle.QuickBattleGame')
t = 0.0
for i in range(60):
    Appc.engine_tick(t, t)
    t = t + 0.1
qb = sys.modules['QuickBattle.QuickBattle']
check('the Quick Battle dialog pane exists', qb.g_pPane is not None)
qb.g_pPane.SetVisible()
MODES = (('4x3', (1024, 768)), ('16x10', (1229, 768)), ('16x9', (1365, 768)), ('21x9', (1792, 768)))
for sMode, kMode in MODES:
    for sPane in ('ships', 'player'):
        sName = '%s_%s_QuickBattle-%s' % (SETUP, sMode, sPane)
        try:
            Appc.engine_set_display(kMode[0], kMode[1])
            ui_display.rebuild_menus()
            qb.g_pPane.SetVisible()
            if sPane == 'player':
                qb.OpenPlayerPane(qb.g_pXO, App.TGEvent_Create())
            for i in range(4):
                Appc.engine_tick(t, t)
                t = t + 0.1
                qb.g_pPane.SetVisible()                # the mission hides the dialog about nine seconds in (a timer), so show it again
                Appc.platform_pump()
                Appc.platform_render(0.0, 0.0, 0.0)
            Appc.platform_snapshot(OUT + '/' + sName + '.raw')
            print 'SNAP', sName
        except:
            tt, v = sys.exc_info()[:2]
            check('%s builds (%s: %s)' % (sName, tt, v), 0)
Appc.platform_close()
print 'RESULT', len(FAILS) and 'FAIL: ' + string.join(FAILS, ', ') or 'PASS', 'quick battle menu test', SETUP
sys.exit(len(FAILS) and 1 or 0)
