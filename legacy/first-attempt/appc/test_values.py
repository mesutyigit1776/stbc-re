# Value-type checks for the Appc engine layer (Python 1.5.2 syntax)
import App
c = App.g_kMainMenuButton1Color
c.r = 0.5
c.a = 1.0
print 'color', c.r, c.g, c.b, c.a
print 'white', App.NiColorA_WHITE.r, App.NiColorA_WHITE.a, 'black a', App.TGColorA_BLACK.a, 'unit_y', App.NiPoint2_UNIT_Y.y
print 'any_target', App.ANY_TARGET, 'infinite_life', App.globals.INFINITE_LIFE
p = App.NiPoint2(3.0, 4.0)
print 'len', p.Length(), 'dot', p.Dot(p)
print 'unitize', p.Unitize(), p.x, p.y
t = App.TGColorA()
t.SetRGBA(0.1, 0.2, 0.3, 0.4)
t.ScaleRGB(2.0)
print 'scaled', t.GetR(), t.GetG(), t.GetB(), t.GetA()
