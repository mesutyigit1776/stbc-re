# Each generated wrapper is a real function, and a call with valid arguments gives the expected result.
import sys, strop, Appc, bc_test

real = bc_test.real_names()
check('wrappers: 184 method names are real functions', len(real) == 184)

traps = []
for name in real:
    try:
        apply(getattr(Appc, name), (1, 2, 3, 4, 5, 6, 7))
    except:
        text = sys.exc_info()[1]
        if type(text) == type('') and strop.find(text, 'not reconstructed') >= 0:
            traps.append(name)
check('wrappers: none of them is a trap', len(traps) == 0)

def near(a, b):
    d = a - b
    if d < 0:
        d = -d
    return d < 0.0001

# interface/color.appc
c = Appc.new_TGColorA()
Appc.TGColorA_SetRGBA(c, 0.25, 0.5, 0.75, 1.0)
check('color: TGColorA SetRGBA and GetG', near(Appc.TGColorA_GetG(c), 0.5))
check('color: TGColorA alias of NiColorA_b_get', near(Appc.TGColorA_b_get(c), 0.75))
Appc.TGColorA_ScaleRGB(c, 2.0)
check('color: TGColorA ScaleRGB', near(Appc.TGColorA_GetR(c), 0.5))
n = Appc.new_NiColorA()
Appc.NiColorA_a_set(n, 0.125)
check('color: NiColorA a_set and a_get', near(Appc.NiColorA_a_get(n), 0.125))
Appc.TGColorA_Copy(c, n)
check('color: TGColorA Copy', near(Appc.TGColorA_GetA(c), 0.125))
Appc.delete_TGColorA(c)
Appc.delete_NiColorA(n)

# interface/math.appc
p = Appc.new_TGPoint3()
Appc.TGPoint3_SetXYZ(p, 3.0, 4.0, 0.0)
check('math: TGPoint3 SetXYZ and GetY', near(Appc.TGPoint3_GetY(p), 4.0))
check('math: TGPoint3 VectorLength', near(Appc.TGPoint3_VectorLength(p), 5.0))
q = Appc.new_NiPoint3(0.0, 0.0, 2.0)
check('math: NiPoint3 Length', near(Appc.NiPoint3_Length(q), 2.0))
check('math: NiPoint3 Dot', near(Appc.NiPoint3_Dot(q, q), 4.0))
cross = Appc.NiPoint3_Cross(q, q)
check('math: NiPoint3 Cross is a pointer string', type(cross) == type('') and cross[0] == '_')
check('math: NiPoint3 Cross of equal vectors is zero', near(Appc.NiPoint3_Length(cross), 0.0))
Appc.TGPoint3_Scale(p, 2.0)
check('math: TGPoint3 Scale', near(Appc.TGPoint3_GetX(p), 6.0))
Appc.delete_NiPoint3(q)
Appc.delete_TGPoint3(p)

# interface/object.appc
check('object: IsIncrementID is an int', type(Appc.TGObject_IsIncrementID()) == type(1))
check('object: GetIndentString of level 1', Appc.TGObject_GetIndentString(1) == '     ')

# interface/modelproperty.appc, subsystemproperty.appc, propertyset.appc, propertymanager.appc
sp = Appc.new_SubsystemProperty()
check('subsystemproperty: new gives a typed pointer string', strop.find(sp, '_p_SubsystemProperty') > 0)
Appc.SubsystemProperty_SetRadius(sp, 1.5)
check('subsystemproperty: SetRadius and GetRadius', near(Appc.SubsystemProperty_GetRadius(sp), 1.5))
Appc.SubsystemProperty_SetCritical(sp, 1)
check('subsystemproperty: SetCritical and IsCritical', Appc.SubsystemProperty_IsCritical(sp) == 1)
check('modelproperty: GetRefCount of a new property', type(Appc.TGModelProperty_GetRefCount(sp)) == type(1))

ps = Appc.new_TGModelPropertySet()
check('propertyset: new gives a typed pointer string', strop.find(ps, '_p_TGModelPropertySet') > 0)
check('propertyset: GetPropertiesByType of an empty set', Appc.TGModelPropertySet_GetPropertiesByType(ps, 1) is not None)

mgr = Appc.globals.g_kModelPropertyManager
check('propertymanager: the global is a typed pointer string', type(mgr) == type('') and strop.find(mgr, '_p_TGModelPropertyManager') > 0)
check('propertymanager: FindByName of an unknown name gives None', Appc.TGModelPropertyManager_FindByName(mgr, 'no such property') is None)
