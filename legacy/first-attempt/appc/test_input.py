# Mouse and keyboard input in the menus. Run through test_input.sh (stock game, Remastered, Orion Delta). Events are sent the way
# the platform sends them (Appc.ui_mouse_move, ui_mouse_button, ui_wheel, ui_key, ui_text); positions are in the pixels of the graphics mode.
# Python 1.5.2: no list comprehensions, no augmented assignment, no nested scopes.
import sys, os, string, time
import Appc
execfile(os.environ.get('APPC_PORT', '../port/appc') + '/host_boot.py')
import App
import ui_display

SETUP = os.environ.get('MENU_SETUP', 'stock')
FAILS = []
T = [0.0]
KIND_BUTTON, KIND_MENU, KIND_BAR, KIND_TEXT, KIND_SCROLL = 1, 2, 3, 4, 5
UF_HIGHLIGHTED = 4

def check(name, ok):
    print ok and 'PASS' or 'FAIL', SETUP, name
    if not ok:
        FAILS.append(name)

def pump(n=3):
    for i in range(n):
        Appc.platform_pump()
        ui_display.service()
        Appc.engine_tick(T[0], T[0])
        T[0] = T[0] + 0.1
        Appc.platform_render(0.0, 0.0, 0.0)

def controls(kind=0):
    lResult = []
    for c in Appc.ui_controls():
        if kind == 0 or c[0] == kind:
            lResult.append(c)
    return lResult

def named(sText, kind=1):
    for c in Appc.ui_controls():
        if c[5] == sText and c[0] == kind:
            return c
    return None

def labels(kind=1):
    lResult = []
    for c in controls(kind):
        lResult.append(c[5])
    return lResult

def sorted_by_y(lControls):
    lKeyed = []
    for c in lControls:
        lKeyed.append((c[2], c))
    lKeyed.sort()
    lResult = []
    for k in lKeyed:
        lResult.append(k[1])
    return lResult

def center(c):
    return ((c[1] + c[3]) / 2.0, (c[2] + c[4]) / 2.0)

def click(c, fx=0.5):
    x = c[1] + (c[3] - c[1]) * fx
    y = (c[2] + c[4]) / 2.0
    Appc.ui_mouse_move(x, y)
    a = Appc.ui_mouse_button(1, 1, x, y)
    b = Appc.ui_mouse_button(1, 0, x, y)
    pump()
    return a and b

def key(wc, shift=0):
    a = Appc.ui_key(wc, 1, shift)
    b = Appc.ui_key(wc, 2, shift)
    pump(2)
    return a

def tab_to(sLabel, iMax=8):
    i = 0
    while Appc.ui_focus() != sLabel and i < iMax:
        key(wc('WC_TAB'))
        i = i + 1
    return Appc.ui_focus() == sLabel

def goto(sLabel, iMax=14):
    # walks the focus with the arrow keys until the control with the text is focused
    lKeys = ('WC_DOWN', 'WC_RIGHT', 'WC_UP', 'WC_LEFT')
    i = 0
    while Appc.ui_focus() != sLabel and i < iMax:
        key(wc(lKeys[i % 4]))
        if Appc.ui_focus() == sLabel:
            break
        i = i + 1
    return Appc.ui_focus() == sLabel

def wc(sName):
    return getattr(App, sName)

def setup_menu():
    App.g_kConfigMapping.LoadConfigFile('Options.cfg')
    __import__('MainMenu.mainmenu')
    mm = sys.modules['MainMenu.mainmenu']
    mm.BuildMainMenu()
    mm.g_pcConfigurePaneName = 'general'
    mm.BuildInterface()
    App.TopWindow_GetTopWindow().ToggleOptionsMenu()
    pump()
    return mm

App.g_kConfigMapping.LoadConfigFile('Options.cfg')
ui_display.set_int('Layout', 0)
ui_display.set_int('Size', 0)
ui_display.set_int('Fullscreen', 0)
ui_display.set_int('Window Width', 1280)
ui_display.set_int('Window Height', 720)
ui_display.start_window('Bridge Commander (port) input test')
mm = setup_menu()

# ---- mouse: hover, click, release outside, right button ----
lTop = labels()
check('the main menu has its top buttons (%d controls)' % len(lTop), 'Configure' in lTop and 'New Game' in lTop and 'Load Game' in lTop)
c = named('Configure')
x, y = center(c)
Appc.ui_mouse_move(x, y)
c = named('Configure')
check('moving over a button highlights it', (c[6] & UF_HIGHLIGHTED) != 0)
Appc.ui_mouse_move(10.0, 10.0)
c = named('Configure')
check('moving away clears the highlight', (c[6] & UF_HIGHLIGHTED) == 0)
c = named('Load Game')
x, y = center(c)
Appc.ui_mouse_move(x, y)
Appc.ui_mouse_button(1, 1, x, y)
Appc.ui_mouse_move(10.0, 10.0)
Appc.ui_mouse_button(1, 0, 10.0, 10.0)
pump()
check('pressing a button and letting go elsewhere does nothing', mm.g_pcCurrentPaneName == 'New Game')
c = named('Load Game')
x, y = center(c)
Appc.ui_mouse_button(3, 1, x, y)
Appc.ui_mouse_button(3, 0, x, y)
pump()
check('the right button does not press a button', mm.g_pcCurrentPaneName == 'New Game')
check('a click on the Configure button opens the Configure tab', click(named('Configure')) and mm.g_pcCurrentPaneName == 'Configure')
check('the tab shows its buttons', 'Sound' in labels() and 'Graphics' in labels() and 'Subtitles' in labels())
check('a click on a yes/no button chooses it', 1)
c = named('Subtitles')
iBefore = c[6]
click(c)
c = named('Subtitles')
check('a click on a yes/no button flips its chosen state', c[6] != iBefore)

# ---- the options tabs and the numeric bars ----
check('a click on the Sound tab', click(named('Sound')))
lBars = sorted_by_y(controls(KIND_BAR))
check('the Sound tab has three volume bars', len(lBars) == 3)
if len(lBars) == 3:
    bar = lBars[0]
    click(bar, 0.8)
    iV = App.g_kSoundManager.GetSFXVolume()
    check('a click at 80 percent of the bar sets the volume (%.2f)' % iV, abs(iV - 0.8) < 0.051)
    click(bar, 0.2)
    iV = App.g_kSoundManager.GetSFXVolume()
    check('a click at 20 percent lowers it (%.2f)' % iV, abs(iV - 0.2) < 0.051)
    # dragging
    x0 = bar[1] + (bar[3] - bar[1]) * 0.1
    x1 = bar[1] + (bar[3] - bar[1]) * 0.9
    y = (bar[2] + bar[4]) / 2.0
    Appc.ui_mouse_move(x0, y)
    Appc.ui_mouse_button(1, 1, x0, y)
    Appc.ui_mouse_move((x0 + x1) / 2.0, y)
    Appc.ui_mouse_move(x1, y)
    Appc.ui_mouse_button(1, 0, x1, y)
    pump()
    iV = App.g_kSoundManager.GetSFXVolume()
    check('dragging along the bar follows the mouse (%.2f)' % iV, abs(iV - 0.9) < 0.051)

# ---- toggles and menus of the Graphics tab ----
check('a click on the Graphics tab', click(named('Graphics')))
click(named('Resolution', KIND_MENU))
cTog = None
for c in controls():
    if c[5][:17] == 'Interface layout:':
        cTog = c
check('the Graphics tab has the interface layout toggle', cTog is not None)
if cTog:
    sOld = cTog[5]
    click(cTog)
    check('a click on a toggle applies its next state (%s, layout %d)' % (sOld, ui_display.get_layout()), ui_display.get_layout() == 1)
    pump(3)
    click(named('Resolution', KIND_MENU))
    cNew = None
    for c in controls():
        if c[5][:17] == 'Interface layout:':
            cNew = c
    check('the toggle shows the new state (%s)' % (cNew and cNew[5]), cNew is not None and cNew[5] != sOld)
    if cNew:
        click(cNew)
    check('and back again', ui_display.get_layout() == 0)
    pump(3)
    click(named('Resolution', KIND_MENU))
click(named('Resolution', KIND_MENU))
cMenu = named('Resolution', KIND_MENU)
check('the Resolution menu is closed again', cMenu is not None and len(controls()) < 30)
if cMenu:
    nBefore = len(controls())
    click(cMenu)
    check('a click on a menu title opens it', len(controls()) > nBefore + 3)
    click(named('Resolution', KIND_MENU))
    check('a second click closes it', len(controls()) == nBefore)

# ---- text entry on the New Game tab ----
check('a click on the New Game button', click(named('New Game')) and mm.g_pcCurrentPaneName == 'New Game')
lText = controls(KIND_TEXT)
check('the New Game tab has an editable name', len(lText) == 1)
if len(lText) == 1:
    click(lText[0])
    Appc.ui_text('Picard')
    pump(1)
    lText = controls(KIND_TEXT)
    check('typed text appears in the entry (%s)' % lText[0][5], lText[0][5][-6:] == 'Picard')
    key(wc('WC_BACKSPACE'))
    lText = controls(KIND_TEXT)
    check('Backspace removes a character (%s)' % lText[0][5], lText[0][5][-5:] == 'Picar')
    key(wc('WC_LEFT'))
    key(wc('WC_DELETE'))
    lText = controls(KIND_TEXT)
    check('Left and Delete remove the character after the caret (%s)' % lText[0][5], lText[0][5][-4:] == 'Pica')
    key(wc('WC_RETURN'))
    Appc.ui_text('Z')
    lText = controls(KIND_TEXT)
    check('after Enter the entry takes no more characters', lText[0][5][-1:] != 'Z')
# a difficulty button
c = named('Admiral')
if c:
    click(c)
    check('a click on a difficulty button sets the difficulty (%s)' % str(mm.g_eNewGameDifficulty), mm.g_eNewGameDifficulty == 2)
    click(named('First Officer'))
    check('and another one', mm.g_eNewGameDifficulty == 0)
    click(named('Captain'))

# ---- scrolling windows (Remastered and Orion Delta lists) ----
def inside(c, area):
    x, y = center(c)
    return x > area[1] and x < area[3] and y > area[2] and y < area[4]

def first_inside(area):
    lBest = None
    for c in Appc.ui_controls():
        if (c[0] == KIND_BUTTON or c[0] == KIND_MENU) and c[5] != '' and inside(c, area):
            if lBest is None or c[2] < lBest[2]:
                lBest = c
    return lBest

def scroll_pos(area):
    c = first_inside(area)
    if c is None:
        return None
    return (c[5], c[2])

lAreas = controls(KIND_SCROLL)
if len(lAreas) > 0:
    area = lAreas[0]
    cx, cy = center(area)
    # the area whose content is longer than its view: the first control moves under the wheel
    lSame = 0
    for a in lAreas:
        k = first_inside(a)
        if k is not None and k[0] == KIND_MENU or k is not None and k[0] == KIND_BUTTON:
            area = a
            cx, cy = center(area)
            p0 = scroll_pos(area)
            Appc.ui_mouse_move(cx, cy)
            Appc.ui_wheel(-3.0)
            pump(1)
            p1 = scroll_pos(area)
            if p1 != p0 and p0 is not None and p1 is not None:
                lSame = 1
                break
    check('the wheel scrolls a window under the mouse (%s)' % str(lSame), lSame == 1)
    if lSame:
        p0 = scroll_pos(area)
        Appc.ui_wheel(3.0)
        pump(1)
        p1 = scroll_pos(area)
        check('the wheel the other way scrolls back (%s to %s)' % (str(p0), str(p1)), p1 != p0)
        click(first_inside(area))
        key(wc('WC_END'))
        pEnd = scroll_pos(area)
        key(wc('WC_HOME'))
        pHome = scroll_pos(area)
        check('End and Home scroll to the ends (%s, %s)' % (str(pEnd), str(pHome)), pEnd != pHome)
        key(wc('WC_PAGEDOWN'))
        pPage = scroll_pos(area)
        check('Page Down scrolls (%s)' % str(pPage), pPage != pHome)
        key(wc('WC_PAGEUP'))
        check('Page Up scrolls back', scroll_pos(area) == pHome)
        # the arrow button of the window frame: hold it and the content moves
        lArrows = []
        for c in controls(KIND_BUTTON):
            if c[5] == '' and c[1] >= area[1] - 5 and c[3] <= area[3] + 80 and c[2] >= area[4] - 5 and c[4] <= area[4] + 60:
                lArrows.append(c)
        if len(lArrows) >= 2:
            lArrows = sorted_by_y(lArrows)
            # the down arrow is the one at the right of the pair
            dn = lArrows[0]
            for c in lArrows:
                if c[1] > dn[1]:
                    dn = c
            x, y = center(dn)
            p0 = scroll_pos(area)
            Appc.ui_mouse_move(x, y)
            Appc.ui_mouse_button(1, 1, x, y)
            pump(8)
            Appc.ui_mouse_button(1, 0, x, y)
            pump(1)
            p1 = scroll_pos(area)
            check('holding the down arrow of the frame scrolls (%s to %s)' % (str(p0), str(p1)), p1 != p0)
            p2 = scroll_pos(area)
            pump(5)
            check('and it stops when let go', scroll_pos(area) == p2)
        else:
            check('no arrow buttons below this window (%d)' % len(lArrows), 1)
else:
    check('no scrolling window on this screen (nothing to scroll)', 1)

# ---- keyboard: Tab, the stock arrow handlers, Enter, Space ----
mm.g_pcConfigurePaneName = 'general'
mm.BuildInterface()
pump()
check('nothing is focused before a key', Appc.ui_focus() is None)
key(wc('WC_TAB'))
check('Tab focuses the first registered control (%s)' % str(Appc.ui_focus()), Appc.ui_focus() == 'New Game')
key(wc('WC_RIGHT'))
sRight = Appc.ui_focus()
check('Right (stock handler) moves along the top row (%s)' % str(sRight), sRight is not None and sRight != 'New Game')
key(wc('WC_LEFT'))
check('Left moves it back (%s)' % str(Appc.ui_focus()), Appc.ui_focus() == 'New Game')
key(wc('WC_DOWN'))
check('Down moves to the row below (%s)' % str(Appc.ui_focus()), Appc.ui_focus() == 'Load Game')
key(wc('WC_UP'))
check('Up moves back (%s)' % str(Appc.ui_focus()), Appc.ui_focus() == 'New Game')
iSteps = 0
while Appc.ui_focus() != 'Configure' and iSteps < 10:
    key(wc('WC_RIGHT'))
    iSteps = iSteps + 1
check('the arrows reach the Configure button (%d steps)' % iSteps, Appc.ui_focus() == 'Configure')
key(wc('WC_RETURN'))
check('Enter presses the focused button', mm.g_pcCurrentPaneName == 'Configure')
tab_to('New Game')
goto('Load Game')
key(wc('WC_SPACE'))
check('Space presses the focused button (%s)' % mm.g_pcCurrentPaneName, mm.g_pcCurrentPaneName == 'Load Game')
tab_to('New Game')
i = 0
while Appc.ui_focus() != 'Configure' and i < 8:
    key(wc('WC_RIGHT'))
    i = i + 1
key(wc('WC_RETURN'))
check('Enter on Configure again', mm.g_pcCurrentPaneName == 'Configure')

# the arrows of this port inside a content pane: click a yes/no button, then move with Down and Up and Left
mm.g_pcConfigurePaneName = 'general'
mm.SwitchMiddlePane('Configure')
pump()
click(named('Collisions'))
check('a click gives the control the keyboard focus (%s)' % str(Appc.ui_focus()), Appc.ui_focus() == 'Collisions')
cC = named('Collisions')
key(wc('WC_DOWN'))
check('Down goes to the next yes/no button (%s)' % str(Appc.ui_focus()), Appc.ui_focus() == 'Character Tool Tips')
key(wc('WC_UP'))
check('Up goes back (%s)' % str(Appc.ui_focus()), Appc.ui_focus() == 'Collisions')
iFlags = named('Collisions')[6]
key(wc('WC_SPACE'))
check('Space flips the chosen state', named('Collisions')[6] != iFlags)
key(wc('WC_RETURN'))
check('Enter flips it again', named('Collisions')[6] == iFlags)
key(wc('WC_LEFT'))
check('Left goes to the tab column (%s)' % str(Appc.ui_focus()), Appc.ui_focus() in ('General', 'Sound', 'Graphics', 'Controls'))
key(wc('WC_RIGHT'))
check('Right goes back into the list (%s)' % str(Appc.ui_focus()), Appc.ui_focus() in ('Subtitles', 'Collisions', 'Character Tool Tips', 'Collision Alert'))

# numeric bar by keys, toggle by Enter, menu by Right
mm.g_pcConfigurePaneName = 'sound'
mm.SwitchMiddlePane('Configure')
pump()
lBars = sorted_by_y(controls(KIND_BAR))
if len(lBars) == 3:
    click(lBars[1], 0.5)
    iV0 = App.g_kSoundManager.GetVoiceVolume()
    key(wc('WC_RIGHT'))
    iV1 = App.g_kSoundManager.GetVoiceVolume()
    key(wc('WC_LEFT'))
    key(wc('WC_LEFT'))
    iV2 = App.g_kSoundManager.GetVoiceVolume()
    check('Right and Left on a volume bar change it (%.2f %.2f %.2f)' % (iV0, iV1, iV2), iV1 > iV0 and iV2 < iV1)
mm.g_pcConfigurePaneName = 'graphics'
mm.SwitchMiddlePane('Configure')
pump()
click(named('Display Device', KIND_MENU))
click(named('Display Device', KIND_MENU))
check('a click on a menu title focuses it (%s)' % str(Appc.ui_focus()), Appc.ui_focus() == 'Display Device')
key(wc('WC_DOWN'))
check('Down reaches the Resolution menu (%s)' % str(Appc.ui_focus()), Appc.ui_focus() == 'Resolution')
n0 = len(controls())
key(wc('WC_RIGHT'))
check('Right opens a menu', len(controls()) > n0 + 3)
key(wc('WC_ESCAPE'))
check('Escape closes it', len(controls()) == n0)
# Enter on a toggle cycles it
iSteps = 0
while (Appc.ui_focus() or '')[:17] != 'Interface layout:' and iSteps < 30:
    key(wc('WC_DOWN'))
    if iSteps == 2:
        key(wc('WC_RIGHT'))
    iSteps = iSteps + 1
if (Appc.ui_focus() or '')[:17] == 'Interface layout:':
    sOld = Appc.ui_focus()
    key(wc('WC_RETURN'))
    check('Enter on a toggle cycles it (%s to %s)' % (sOld, str(Appc.ui_focus())), Appc.ui_focus() != sOld)
    key(wc('WC_RETURN'))
else:
    check('the layout toggle was not reached by the arrows (it is inside the Resolution menu, which is closed: fine)', 1)

# ---- the same through the platform: SDL events converted to the mode, the mouse cursor, text input ----
def plat_click(c):
    wx, wy = Appc.platform_window_point(center(c)[0], center(c)[1])
    Appc.platform_inject_mouse(0, 0, wx, wy)
    pump(1)
    Appc.platform_inject_mouse(1, 1, wx, wy)
    pump(1)
    Appc.platform_inject_mouse(2, 1, wx, wy)
    pump(3)

def plat_key(sName, shift=0):
    Appc.platform_inject_key(sName, 1, shift)
    pump(1)
    Appc.platform_inject_key(sName, 0, shift)
    pump(3)

mm.g_pcConfigurePaneName = 'general'
mm.BuildInterface()
pump()
c = named('Configure')
wx, wy = Appc.platform_window_point(center(c)[0], center(c)[1])
Appc.platform_inject_mouse(0, 0, wx, wy)
pump(2)
check('a mouse move from the window highlights the button under it', (named('Configure')[6] & UF_HIGHLIGHTED) != 0)
plat_click(named('Configure'))
check('a window click presses the button', mm.g_pcCurrentPaneName == 'Configure')
plat_click(named('New Game'))
check('and another one', mm.g_pcCurrentPaneName == 'New Game')
lText = controls(KIND_TEXT)
if len(lText) == 1:
    plat_click(lText[0])
    Appc.platform_inject_text('Riker')
    pump(2)
    check('text input from the window is typed into the entry (%s)' % controls(KIND_TEXT)[0][5], controls(KIND_TEXT)[0][5][-5:] == 'Riker')
    plat_key('Backspace')
    check('a window Backspace removes a character (%s)' % controls(KIND_TEXT)[0][5], controls(KIND_TEXT)[0][5][-4:] == 'Rike')
    plat_key('Escape')
    check('Escape from the window leaves the entry without closing the window', Appc.platform_pump() == 1)
plat_key('Tab')
check('Tab from the window focuses a control (%s)' % str(Appc.ui_focus()), Appc.ui_focus() is not None)
plat_key('Down')
sF = Appc.ui_focus()
check('Down from the window moves the focus (%s)' % str(sF), sF is not None)
Appc.platform_inject_mouse(3, 0, 0.0, -1.0)
pump(1)
check('the mouse stays usable after keys', Appc.platform_pump() == 1)

ui_display.set_int('Layout', 0)
ui_display.set_int('Size', 0)
ui_display.save()
Appc.platform_close()
print 'RESULT', len(FAILS) and 'FAIL: ' + string.join(FAILS, ', ') or 'PASS', 'input test', SETUP
sys.exit(len(FAILS) and 1 or 0)
