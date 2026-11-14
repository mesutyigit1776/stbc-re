# The pointer strings of the SWIG runtime.
import sys, strop, Appc

def raised(func, *args):
    try:
        apply(func, args)
    except:
        return sys.exc_info()
    return None

p = Appc.new_TGPoint3()
check('pointer string starts with an underscore', p[0] == '_')
check('pointer string ends with the type name', p[-11:] == '_p_TGPoint3')
digits = p[1:-11]
ok = len(digits) > 0
for ch in digits:
    if strop.find('0123456789abcdef', ch) < 0:
        ok = 0
check('pointer string digits are lower case hex', ok)
check('pointer string has no leading zero', digits[0] != '0')

Appc.TGPoint3_SetXYZ(p, 1.0, 2.0, 3.0)
check('a derived type is accepted: TGPoint3 as NiPoint3', Appc.NiPoint3_x_get(p) == 1.0)

info = raised(Appc.TGPoint3_GetX, 5)
check('a number is not a pointer', info is not None and info[1] == 'Type error. Expected _p_TGPoint3')
info = raised(Appc.TGPoint3_GetX, '_1234_p_NiColorA')
check('a pointer of another type', info is not None and info[1] == 'Type error. Expected _p_TGPoint3')
info = raised(Appc.TGPoint3_GetX, 'text')
check('a text that is no pointer', info is not None and info[1] == 'Type error. Expected _p_TGPoint3')
check('None is a null pointer for a pointer argument', Appc.TGModelProperty_Cast(None) is None)
check('NULL text is a null pointer for a pointer argument', Appc.TGModelProperty_Cast('NULL') is None)

class Shadow:
    def __init__(self, this):
        self.this = this
check('an object with a this attribute is accepted', Appc.TGPoint3_GetX(Shadow(p)) == 1.0)

check('an upper case digit stops the digits', raised(Appc.TGPoint3_GetX, '_ABC_p_TGPoint3') is not None)
Appc.delete_TGPoint3(p)
