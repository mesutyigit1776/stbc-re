# Menu rendering and display tests. Run through test_menus.sh, which sets the game root for the stock game,
# Remastered or Orion Delta. Writes one snapshot per screen and mode into $MENU_OUT and prints PASS or FAIL lines.
# Python 1.5.2: no list comprehensions, no augmented assignment.
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

def frames(n, service=0):
    for i in range(n):
        Appc.platform_pump()
        if service:
            ui_display.service()
        Appc.engine_tick(frames_t[0], frames_t[0])
        frames_t[0] = frames_t[0] + 0.1
        Appc.platform_render(0.0, 0.0, 0.0)
        time.sleep(0.02)
frames_t = [0.0]

App.g_kConfigMapping.LoadConfigFile('Options.cfg')
# fixed display settings for the run, restored at the end
SAVED = (ui_display.get_int('Layout', 0), ui_display.get_int('Size', 0), ui_display.get_int('Window Width', 1280), ui_display.get_int('Window Height', 720), ui_display.get_int('Fullscreen', 0))
ui_display.set_int('Layout', 0)
ui_display.set_int('Size', 0)
ui_display.set_int('Fullscreen', 0)
ui_display.set_int('Window Width', 1280)
ui_display.set_int('Window Height', 720)
ui_display.start_window('Bridge Commander (port) menu test')
check('window opened with a UI mode', ui_display.g_kMode[0] > 0 and ui_display.g_kMode[1] > 0)

__import__('MainMenu.mainmenu')
mm = sys.modules['MainMenu.mainmenu']

# fake save games, so the Load Game list has something to show
if not os.path.isdir('saves'):
    os.mkdir('saves')
    MADE_SAVES = 1
else:
    MADE_SAVES = 0
for n in ('Alpha Run', 'Beta Run', 'Gamma Run'):
    f = open('saves/' + n + '.BCS', 'w')
    f.write('x')
    f.close()

MODES = (('4x3', (1024, 768)), ('16x10', (1229, 768)), ('16x9', (1365, 768)), ('21x9', (1792, 768)))
PANES = (('New Game', 'general'), ('Load Game', 'general'), ('Configure', 'general'), ('Configure', 'sound'), ('Configure', 'graphics'), ('Configure', 'keyboard'))

for sMode, kMode in MODES:
    Appc.engine_set_display(kMode[0], kMode[1])
    for sPane, sCfg in PANES:
        sName = '%s_%s_%s' % (SETUP, sMode, string.replace(sPane + '-' + sCfg, ' ', ''))
        try:
            mm.g_pcConfigurePaneName = sCfg
            mm.BuildInterface()
            mm.SwitchMiddlePane(sPane)
            top = App.TopWindow_GetTopWindow()
            opt = top.FindMainWindow(App.MWT_OPTIONS)
            if not opt.IsVisible():
                top.ToggleOptionsMenu()
            frames(3)
            Appc.platform_snapshot(OUT + '/' + sName + '.raw')
            print 'SNAP', sName
        except:
            t, v = sys.exc_info()[:2]
            check('%s builds (%s: %s)' % (sName, t, v), 0)

# the file list shows the save files
mm.g_pcConfigurePaneName = 'general'
Appc.engine_set_display(1024, 768)
mm.BuildInterface()
mm.SwitchMiddlePane('Load Game')
def find_buttons(o, out):
    b = App.STButton_Cast(o)
    if b:
        n = App.TGString()
        b.GetName(n)
        out.append(n.GetCString())
    pp = App.TGPane_Cast(o)
    if pp:
        for i in range(pp.GetNumChildren()):
            c = pp.GetNthChild(i)
            if c:
                find_buttons(c, out)
lNames = []
find_buttons(App.TopWindow_GetTopWindow().FindMainWindow(App.MWT_OPTIONS), lNames)
check('Load Game lists the three save files', 'Alpha Run.BCS' in lNames and 'Beta Run.BCS' in lNames and 'Gamma Run.BCS' in lNames)
for n in ('Alpha Run', 'Beta Run', 'Gamma Run'):
    os.remove('saves/' + n + '.BCS')
if MADE_SAVES:
    os.rmdir('saves')

# display settings: pressing the Graphics tab buttons changes the window, the mode and the saved settings
mm.g_pcConfigurePaneName = 'graphics'
ui_display.refit()
Appc.engine_set_display(ui_display.g_kMode[0], ui_display.g_kMode[1])
mm.BuildInterface()
mm.SwitchMiddlePane('Configure')
def find_named(o, sLabel, lFound):
    b = App.STButton_Cast(o)
    if b:
        n = App.TGString()
        b.GetName(n)
        if n.GetCString() == sLabel:
            lFound.append(b)
    pp = App.TGPane_Cast(o)
    if pp:
        for i in range(pp.GetNumChildren()):
            c = pp.GetNthChild(i)
            if c:
                find_named(c, sLabel, lFound)
def press(sLabel):
    lFound = []
    find_named(App.TopWindow_GetTopWindow().FindMainWindow(App.MWT_OPTIONS), sLabel, lFound)
    if not lFound:
        return 0
    lFound[0].SendActivationEvent()
    frames(25, 1)
    return 1
iDeskW, iDeskH = Appc.platform_display_size()
lRes = ui_display.resolutions()
check('the resolution list is not empty and fits the desktop', len(lRes) > 0 and lRes[len(lRes) - 1][0] <= iDeskW)
sLow = '%d x %d' % lRes[0]
check('press the smallest resolution (%s)' % sLow, press(sLow))
check('the saved window size follows (%s)' % str((ui_display.get_int('Window Width', 0), ui_display.get_int('Window Height', 0))), (ui_display.get_int('Window Width', 0), ui_display.get_int('Window Height', 0)) == lRes[0])
iW, iH = Appc.platform_window_size()
check('the UI mode has the window shape', abs(float(ui_display.g_kMode[0]) / ui_display.g_kMode[1] - float(iW) / iH) < 0.02 or ui_display.g_kMode[0] == (ui_display.g_kMode[1] * 4) / 3)
check('press the layout toggle (4:3)', press('Interface layout: AUTO'))
check('layout 4:3 gives a 4:3 mode', ui_display.get_layout() == 1 and ui_display.g_kMode[0] * 3 == ui_display.g_kMode[1] * 4)
check('press the layout toggle (auto)', press('Interface layout: 4:3'))
check('layout auto is saved', ui_display.get_layout() == 0)
iH0 = ui_display.g_kMode[1]
check('press the size toggle (large)', press('Interface size: NORMAL'))
check('large gives a smaller mode', ui_display.get_size() == 1 and ui_display.g_kMode[1] <= iH0)
check('press the size toggle twice (small, normal)', press('Interface size: LARGE') and press('Interface size: SMALL'))
check('normal size is back', ui_display.get_size() == 0 and ui_display.g_kMode[1] == iH0)

# full screen (only with MENU_FULLSCREEN=1: it switches the whole screen for a moment)
if os.environ.has_key('MENU_FULLSCREEN'):
    iW0, iH0 = Appc.platform_window_size()
    check('press the full screen toggle', press('Display mode: WINDOW'))
    frames(60, 1)
    iWf, iHf = Appc.platform_window_size()
    check('full screen changes the drawable (%s to %s)' % (str((iW0, iH0)), str((iWf, iHf))), (iWf, iHf) != (iW0, iH0))
    check('the UI mode follows full screen', abs(float(ui_display.g_kMode[0]) / ui_display.g_kMode[1] - float(iWf) / iHf) < 0.05)
    check('press the window toggle', press('Display mode: FULL SCREEN'))
    frames(60, 1)
    iW1, iH1 = Appc.platform_window_size()
    check('back to a window (%s)' % str((iW1, iH1)), (iW1, iH1) != (iWf, iHf))

# credits: lines come and go on time
mm.g_pcConfigurePaneName = 'general'
mm.BuildInterface()
App.TopWindow_GetTopWindow().ToggleOptionsMenu()
opt = App.TopWindow_GetTopWindow().FindMainWindow(App.MWT_OPTIONS)
if not opt.IsVisible():
    App.TopWindow_GetTopWindow().ToggleOptionsMenu()
mm.Credits(opt, App.TGEvent_Create())
seen = 0
for i in range(100):
    frames(1)
    pPane = App.TGPane_Cast(App.TGObject_GetTGObjectPtr(mm.g_idCreditPane))
    if pPane and pPane.GetNumChildren() > 0:
        seen = seen + 1
check('credit lines appear in the credits pane', seen > 0)

# put the display settings back
ui_display.set_int('Layout', SAVED[0])
ui_display.set_int('Size', SAVED[1])
ui_display.set_int('Window Width', SAVED[2])
ui_display.set_int('Window Height', SAVED[3])
ui_display.set_int('Fullscreen', SAVED[4])
ui_display.save()
Appc.platform_close()
print 'RESULT', len(FAILS) and 'FAIL: ' + string.join(FAILS, ', ') or 'PASS', 'menu test', SETUP
sys.exit(len(FAILS) and 1 or 0)
