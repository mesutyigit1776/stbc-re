import marshal
import struct
import strop

check('repr gives a 3 digit exponent', repr(6.7e-05) == '6.7e-005')
check('str gives a 3 digit exponent', str(6.7e-05) == '6.7e-005')
check('large exponent', repr(1e100) == '1e+100' and repr(1.5e-300) == '1.5e-300')
check('whole numbers get .0', repr(3.0) == '3.0' and repr(-12.0) == '-12.0' and repr(1e15) == '1e+015')
check('twelve digits', repr(0.1) == '0.1' and repr(1.0 / 3) == '0.333333333333')
check('negative zero has no sign', repr(-0.0) == '0.0')

check('%g', '%g' % 6.7e-05 == '6.7e-005' and '%g' % 100000.0 == '100000' and '%g' % 1000000.0 == '1e+006')
check('%e', '%e' % 1.5 == '1.500000e+000' and '%e' % 1e100 == '1.000000e+100' and '%E' % 0.0 == '0.000000E+000')
check('%f', '%f' % 2.5 == '2.500000' and '%.2f' % 1e20 == '100000000000000000000.00')
check('%f gives zeros after 17 digits', '%f' % 66926478731690.96 == '66926478731690.961000')
check('%.17g', '%.17g' % 0.1 == '0.10000000000000001' and '%.17g' % 6.7e-05 == '6.7000000000000002e-005')
half = struct.unpack('d', '\x4c\x9c\xe4\x35\x5f\x21\xea\x42')[0]
check('%.17g of a half', '%.17g' % half == '229845068883170.37')
check('%.12g', '%.12g' % 123456789.123456789 == '123456789.123')
check('%.0f rounds a half up', '%.0f' % 2.5 == '3' and '%.0f' % 0.5 == '1' and '%.2f' % 0.125 == '0.13')
check('%.1g', '%.1g' % 0.95 == '0.9' and '%#.3g' % 1.0 == '1.00')
check('%#.0f', '%#.0f' % 3.0 == '3.')
check('%-10.2f padding', '%-10.2f|' % 1.5 == '1.50      |' and '%010.3f' % -1.5 == '-00001.500')
check('%s of float', '%s' % 6.7e-05 == '6.7e-005')

check('float() reads a 3 digit exponent', float('6.7e-005') == 6.7e-05)
check('float() reads spaces', float('  12.5  ') == 12.5)
try:
    float('1e400')
    big = 'no error'
except ValueError, msg:
    big = 'error'
check('float(1e400) is an error', big == 'error')
try:
    float('1e-400')
    small = 'no error'
except ValueError, msg:
    small = 'error'
check('float(1e-400) is an error', small == 'error')
try:
    float('abc')
    bad = 'no error'
except ValueError, msg:
    bad = str(msg)
check('float(abc) message', bad == 'invalid literal for float(): abc')
check('float of a halfway text rounds down', float('9007199254740993.5') == 9007199254740992.0)
check('float of a long text', float('0.1000000000000000055511151231257827') == 0.1)
check('strop.atof', strop.atof('6.7e-005') == 6.7e-05)
check('compiled literals', 6.7e-005 == float('6.7e-5') and 1e-400 == 0.0)

packed = marshal.dumps(6.7e-05)
check('marshal text', packed == 'f' + chr(8) + '6.7e-005')
check('marshal round trip', marshal.loads(packed) == 6.7e-05 and marshal.loads(marshal.dumps(-2.5e300)) == -2.5e300)
check('marshal of a whole number', marshal.dumps(3.0) == 'f' + chr(3) + '3.0')

big = 1e308 * 10.0
check('inf text', repr(big) == '1.#INF' and repr(-big) == '-1.#INF')
check('inf in a format', '%f' % big == '1.#INF' and '%g' % big == '1.#INF' and '%e' % -big == '-1.#INF00e+000')
qnan = struct.unpack('d', '\x00\x00\x00\x00\x00\x00\xf8\x7f')[0]
ind = struct.unpack('d', '\x00\x00\x00\x00\x00\x00\xf8\xff')[0]
snan = struct.unpack('d', '\x00\x00\x00\x00\x00\x00\xf4\x7f')[0]
check('nan texts', repr(qnan) == '1.#QNAN' and repr(ind) == '-1.#IND' and repr(snan) == '1.#SNAN')
check('nan in marshal', marshal.dumps(qnan) == 'f' + chr(7) + '1.#QNAN')
check('complex', repr(complex(6.7e-05, 1.5)) == '(6.7e-005+1.5j)' and repr(complex(0, 1e-100)) == '1e-100j')
