# Display settings and fitting of the UI to the window (port only; the original had a fixed list of 4:3 modes).
#
# The UI is laid out by the stock scripts as fractions of the "graphics mode" (App.GraphicsModeInfo). The port picks that mode from the
# window: its height is the largest of 480, 600, 768, 1024 and 1200 that fits the window (one step lower for "Large", one higher for
# "Small"), and its width follows the window shape (layout "Auto") or is four thirds of the height (layout "4:3", centred in the
# window with the sides left black). The platform draws the mode scaled to the window.
#
# Settings live in Options.cfg, section "Port Display": Layout (0 Auto, 1 4:3), Size (-1 Small, 0 Normal, 1 Large), Fullscreen (0 or 1),
# Window Width and Window Height (points). Python 1.5.2: no list comprehensions and no augmented assignment here.
import Appc
import string

SECTION = 'Port Display'
BUCKETS = (480, 600, 768, 1024, 1200)
LAYOUT_AUTO = 0
LAYOUT_43 = 1

# last mode set, to tell the caller whether the UI has to be rebuilt
g_kMode = (0, 0)


def _cfg():
    import App
    return App.g_kConfigMapping


def get_int(sKey, iDefault):
    kCfg = _cfg()
    if kCfg.HasValue(SECTION, sKey):
        return kCfg.GetIntValue(SECTION, sKey)
    return iDefault


def set_int(sKey, iValue):
    _cfg().SetIntValue(SECTION, sKey, iValue)


def save():
    # Options.cfg is also written by the options menu; a write here only adds the section
    _cfg().SaveConfigFile('Options.cfg')


def mode_for(iWinW, iWinH, iLayout=LAYOUT_AUTO, iSize=0):
    # the graphics mode (width, height) for a window of iWinW x iWinH pixels
    if iWinH < 1:
        iWinH = 1
    if iWinW < 1:
        iWinW = 1
    iIndex = 0
    for i in range(len(BUCKETS)):
        if BUCKETS[i] <= iWinH:
            iIndex = i
    iIndex = iIndex - iSize
    if iIndex < 0:
        iIndex = 0
    if iIndex > len(BUCKETS) - 1:
        iIndex = len(BUCKETS) - 1
    iH = BUCKETS[iIndex]
    if iLayout == LAYOUT_43:
        iW = (iH * 4) / 3
    else:
        iW = int(iH * float(iWinW) / float(iWinH) + 0.5)
        # not narrower than 4:3 (the stock layout needs that much room) and not wider than 32:9
        if iW < (iH * 4) / 3:
            iW = (iH * 4) / 3
        if iW > (iH * 32) / 9:
            iW = (iH * 32) / 9
    return (iW, iH)


def fit():
    # sets the graphics mode for the window as it is now; returns 1 when the mode changed (then the menus must be rebuilt)
    global g_kMode
    iWinW, iWinH = Appc.platform_window_size()
    if iWinW <= 0 or iWinH <= 0:
        return 0
    kMode = mode_for(iWinW, iWinH, get_int('Layout', LAYOUT_AUTO), get_int('Size', 0))
    if kMode == g_kMode:
        return 0
    g_kMode = kMode
    Appc.engine_set_display(kMode[0], kMode[1])
    import lcars_fit
    lcars_fit.fit_all()
    return 1


def start_window(sTitle):
    # opens the window at the saved size (or 1280 x 720, or smaller on a small desktop) and fits the UI to it
    iDesktopW, iDesktopH = Appc.platform_display_size()
    iW = get_int('Window Width', 1280)
    iH = get_int('Window Height', 720)
    if iW > iDesktopW:
        iW = iDesktopW
    if iH > iDesktopH:
        iH = iDesktopH
    Appc.platform_open(sTitle, iW, iH)
    if get_int('Fullscreen', 0):
        Appc.platform_set_window(iW, iH, 1)
    fit()


def resolutions():
    # the window sizes the Graphics tab offers: the display's own modes, and the common wide and 4:3 sizes that fit the desktop
    iDesktopW, iDesktopH = Appc.platform_display_size()
    lCommon = ((1280, 720), (1366, 768), (1440, 900), (1600, 900), (1680, 1050), (1920, 1080), (1920, 1200), (2560, 1440),
               (2560, 1600), (3440, 1440), (3840, 2160), (1024, 768), (1280, 960), (1280, 1024), (1600, 1200))
    lResult = []
    for kRes in lCommon:
        if kRes[0] <= iDesktopW and kRes[1] <= iDesktopH:
            lResult.append(kRes)
    for kRes in Appc.platform_display_modes():
        if kRes[0] <= iDesktopW and kRes[1] <= iDesktopH and kRes not in lResult:
            lResult.append(kRes)
    if (iDesktopW, iDesktopH) not in lResult:
        lResult.append((iDesktopW, iDesktopH))
    # smallest first, by height then width
    lKeyed = []
    for kRes in lResult:
        lKeyed.append((kRes[1], kRes[0]))
    lKeyed.sort()
    lResult = []
    for kKey in lKeyed:
        lResult.append((kKey[1], kKey[0]))
    return lResult


def rebuild_menus():
    # the main menu is built from the graphics mode, so a new mode needs it built again (the menu that was open stays open)
    import sys
    if sys.modules.has_key('MainMenu.mainmenu'):
        mm = sys.modules['MainMenu.mainmenu']
        sPane = mm.g_pcCurrentPaneName
        mm.BuildInterface()
        if sPane:
            mm.SwitchMiddlePane(sPane)
    # the Quick Battle setup windows are built from the mode too (BuildDialog keeps its pane when it gets 1)
    for sName in ('QuickBattle.QuickBattle',):
        if sys.modules.has_key(sName):
            qb = sys.modules[sName]
            if hasattr(qb, 'g_pPane') and qb.g_pPane and hasattr(qb, 'BuildDialog'):
                # Orion Delta's ship pack (Custom.Bedrock) remembers the race menus it made; a second build must start from none
                if sys.modules.has_key('Custom.Bedrock.BedrockMain'):
                    bm = sys.modules['Custom.Bedrock.BedrockMain']
                    for sList in ('RaceList', 'SubMenuList', 'SubMenuListP'):
                        if hasattr(bm, sList):
                            setattr(bm, sList, [])
                try:
                    qb.BuildDialog(1)
                except:
                    t, v = sys.exc_info()[:2]
                    print 'ui_display: the Quick Battle dialog could not be rebuilt:', t, v


def apply_window(iW, iH, iFullscreen=0):
    # a new window size from the Graphics tab: the setting is saved, the window changes and the UI is fitted and rebuilt
    set_int('Window Width', iW)
    set_int('Window Height', iH)
    set_int('Fullscreen', iFullscreen)
    save()
    Appc.platform_set_window(iW, iH, iFullscreen)
    # the window changes size a moment later; service() sees that and fits the UI
    return 1


def apply_resolution_index(iIndex):
    lRes = resolutions()
    if iIndex < 0 or iIndex >= len(lRes):
        return 0
    return apply_window(lRes[iIndex][0], lRes[iIndex][1], get_int('Fullscreen', 0))


def service():
    # called once per frame by a host: fits the UI again when the window changed size; returns 1 when the menus were rebuilt
    if Appc.platform_resized():
        return refit()
    return 0


def refit():
    # called when the drawable size changed (the user dragged the window, or a setting changed)
    if fit():
        rebuild_menus()
        send_mode_events()
        return 1
    return 0


def send_mode_events():
    # as GraphicsMenu::ChangeDisplayMode 00554040 does after a mode change: the icon groups, then the size, then the positions of the
    # interface (the tactical window rebuilds its displays from them)
    import App
    pMode = App.GraphicsModeInfo_GetCurrentMode()
    iIndex = pMode.GetCurrentResolution()
    for eType in (App.ET_UI_SWITCH_ICON_GROUPS, App.ET_UI_RESIZE, App.ET_UI_REPOSITION):
        pEvent = App.TGIntEvent_Create()
        pEvent.SetEventType(eType)
        pEvent.SetInt(iIndex)
        App.g_kEventManager.AddEvent(pEvent)


# ---- what the Graphics tab reads and sets ----
def get_layout():
    return get_int('Layout', LAYOUT_AUTO)


def get_size():
    return get_int('Size', 0)


def get_fullscreen():
    return get_int('Fullscreen', 0)


def current_resolution_index():
    # the index in resolutions() of the saved window size, or -1
    iW = get_int('Window Width', 1280)
    iH = get_int('Window Height', 720)
    lRes = resolutions()
    for i in range(len(lRes)):
        if lRes[i] == (iW, iH):
            return i
    return -1


def apply_layout(iLayout):
    set_int('Layout', iLayout)
    save()
    return refit()


def apply_size(iSize):
    set_int('Size', iSize)
    save()
    return refit()


def apply_fullscreen(iFullscreen):
    iW = get_int('Window Width', 1280)
    iH = get_int('Window Height', 720)
    set_int('Fullscreen', iFullscreen)
    save()
    Appc.platform_set_window(iW, iH, iFullscreen)
    return 1
