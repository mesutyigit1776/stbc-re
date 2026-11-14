# Pixel-true widths for the wide UI modes (port only).
#
# The LCARS modules (Icons.LCARS_640 .. LCARS_1600) give the sizes of the tactical windows as pixels of their own 4:3 screen divided by
# SCREEN_PIXEL_WIDTH (for example SHIELDS_DISPLAY_WIDTH = 146 / SCREEN_PIXEL_WIDTH). The icons are sized in pixels of the real mode
# (TGIcon: artwork pixels / mode pixels), so in a wider mode a window would be wider than its artwork. This module rescales those
# horizontal sizes to the width of the mode, so a window keeps its pixel size (the heights already match: the mode height is the
# module's height). The values are recomputed from the originals whenever the mode changes.
# Python 1.5.2: no list comprehensions, no augmented assignment.
import sys

# the names of the horizontal sizes: every LCARS constant that the modules write as pixels / SCREEN_PIXEL_WIDTH
HORIZ = ('DEFAULT_ST_INDENT_HORIZ', 'MARKER_INDENT_HORIZ', 'TAC_WEAPONS_CTRL_INDENT_HORIZ', 'REPAIR_ST_INDENT_HORIZ',
         'TACTICAL_MENU_WIDTH', 'SHIELDS_DISPLAY_WIDTH', 'DAMAGE_DISPLAY_WIDTH', 'WEAPONS_PANE_WIDTH', 'WEAPONS_CTRL_PANE_WIDTH',
         'PICARD_MENU_WIDTH', 'WEAPONS_DISPLAY_GAP_X', 'RADAR_SCOPE_WIDTH', 'RADAR_TOGGLE_WIDTH', 'SIDE_SHIELDS_X_OFFSET',
         'POWER_GAUGE_WIDTH', 'ELBOW_THICKNESS_WIDTH', 'STBUTTON_CHECK_OFFSET_X', 'TOOL_TIP_WIDTH')


def fit_module(mod):
    import App
    iW = App.GraphicsModeInfo_GetCurrentMode().GetWidth()
    if iW < 1:
        return
    if not hasattr(mod, '_fit_orig'):
        kOrig = {'SCREEN_PIXEL_WIDTH': mod.SCREEN_PIXEL_WIDTH}
        for sName in HORIZ:
            if hasattr(mod, sName):
                kOrig[sName] = getattr(mod, sName)
        mod._fit_orig = kOrig
    kOrig = mod._fit_orig
    fScale = kOrig['SCREEN_PIXEL_WIDTH'] / float(iW)
    for sName in HORIZ:
        if kOrig.has_key(sName):
            setattr(mod, sName, kOrig[sName] * fScale)
    mod.SCREEN_PIXEL_WIDTH = float(iW)


def fit_all():
    # every LCARS module that is loaded, for the mode that is current now
    try:
        fit_tables()
    except:
        pass
    for sName in sys.modules.keys():
        if sName[:11] == 'Icons.LCARS' and sys.modules[sName] is not None and hasattr(sys.modules[sName], 'SCREEN_PIXEL_WIDTH'):
            try:
                fit_module(sys.modules[sName])
            except:
                pass


# Tables that the stock scripts index by the width of the mode (640, 800, 1024, 1280 or 1600): a wide mode has no entry. The entry
# of the wide width is made from the entry of the 4:3 mode with the same height.
DESIGN_WIDTHS = (640, 800, 1024, 1280, 1600)
DESIGN_HEIGHTS = (480, 600, 768, 1024, 1200)
TABLES = (('Tactical.Interface.WeaponsDisplay', ('dTORP_X_BIAS', 'dTORP_Y_BIAS', 'dPHASER_X_BIAS', 'dPHASER_Y_BIAS')),)


def design_width():
    import App
    iH = App.GraphicsModeInfo_GetCurrentMode().GetHeight()
    iBest = 0
    for i in range(len(DESIGN_HEIGHTS)):
        if abs(DESIGN_HEIGHTS[i] - iH) < abs(DESIGN_HEIGHTS[iBest] - iH):
            iBest = i
    return DESIGN_WIDTHS[iBest]


def fit_tables():
    import App
    iW = App.GraphicsModeInfo_GetCurrentMode().GetWidth()
    iDesign = design_width()
    for kEntry in TABLES:
        if sys.modules.has_key(kEntry[0]) and sys.modules[kEntry[0]] is not None:
            kMod = sys.modules[kEntry[0]]
            for sName in kEntry[1]:
                if hasattr(kMod, sName):
                    kTable = getattr(kMod, sName)
                    if kTable.has_key(iDesign):
                        kTable[iW] = kTable[iDesign]
    # the import hook runs this only until the stock module is loaded and patched once
    if sys.modules.has_key('Tactical.Interface.WeaponsDisplay'):
        sys.modules['lcars_fit_done'] = sys.modules['lcars_fit']
