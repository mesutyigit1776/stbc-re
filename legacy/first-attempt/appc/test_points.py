import App
a = App.TGPoint3(); a.SetXYZ(1.0, 0.0, 0.0)
b = App.TGPoint3(); b.SetXYZ(0.0, 1.0, 0.0)
c = a.Cross(b)
print 'cross', c.GetX(), c.GetY(), c.GetZ()
a.Add(b); print 'add', a.GetX(), a.GetY(), a.GetZ()
a.Scale(2.0); print 'scale', a.GetX(), a.GetY(), a.GetZ(), 'len', App.TGPoint3_VectorLength(a)
App.TGPoint3_UnitizeVector(a); print 'unit', a.GetX(), a.GetY()
n = App.NiPoint3(3.0, 4.0, 0.0); print 'ni len', n.Length(), n.Unitize(), n.x, n.y
p = App.TGPoint3(); p.SetXYZ(2.0, 2.0, 0.0)
q = App.TGPoint3(); q.SetXYZ(1.0, 0.0, 0.0)
al = p.GetAlignedComponent(q); pe = p.GetPerpendicularComponent(q)
print 'aligned', al.GetX(), al.GetY(), 'perp', pe.GetX(), pe.GetY()
