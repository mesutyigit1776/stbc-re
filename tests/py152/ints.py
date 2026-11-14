import array
import marshal
import struct
import sys

# The long of the exe has 32 bits. These checks run the scripts-visible integer behaviour of the interpreter.
# The expected values follow the 32-bit arithmetic of the exe functions (see tests/emu/tests/py_long.py).


def outcome(source):
    try:
        return repr(eval(source))
    except OverflowError, msg:
        return 'OverflowError: %s' % msg
    except ValueError, msg:
        return 'ValueError: %s' % msg
    except ZeroDivisionError, msg:
        return 'ZeroDivisionError: %s' % msg
    except TypeError, msg:
        return 'TypeError: %s' % msg


MIN = -2147483647 - 1

check('maxint', sys.maxint == 2147483647 and repr(sys.maxint) == '2147483647')
check('minint', MIN == -sys.maxint - 1 and repr(MIN) == '-2147483648')

check('add overflows', outcome('2147483647 + 1') == 'OverflowError: integer addition')
check('add negative overflows', outcome('-2147483647 - 1 + -1') == 'OverflowError: integer addition')
check('subtract overflows', outcome('-2147483647 - 2') == 'OverflowError: integer subtraction')
check('multiply overflows', outcome('46341 * 46341') == 'OverflowError: integer multiplication')
check('multiply fits', outcome('46340 * 46340') == '2147395600')
check('multiply by min', outcome('(-2147483647 - 1) * -1') == 'OverflowError: integer multiplication'
      and outcome('(-2147483647 - 1) * 1') == '-2147483648')
check('negate min', outcome('-(-2147483647 - 1)') == 'OverflowError: integer negation')
check('abs min', outcome('abs(-2147483647 - 1)') == 'OverflowError: integer negation')
check('power overflows', outcome('2 ** 31') == 'OverflowError: integer pow()' and outcome('2 ** 30') == '1073741824')
check('power of minus two', outcome('(-2) ** 31') == '-2147483648')
check('power with modulus', outcome('pow(2, 31, 1000)') == '648' and outcome('pow(3, 100, 7)') == '4')
check('negative power', outcome('2 ** -1') == 'ValueError: integer to the negative power')

check('division rounds down', outcome('-7 / 2') == '-4' and outcome('7 / -2') == '-4' and outcome('-7 / -2') == '3')
check('modulo has the sign of the divisor', outcome('-7 % 2') == '1' and outcome('7 % -2') == '-1')
check('divmod', outcome('divmod(-7, 2)') == '(-4, 1)' and outcome('divmod(7, -2)') == '(-4, -1)')
check('minint divided by two', outcome('(-2147483647 - 1) / 2') == '-1073741824'
      and outcome('(-2147483647 - 1) % 2') == '0')
check('minint divided by itself', outcome('(-2147483647 - 1) / (-2147483647 - 1)') == '1')
check('division by zero', outcome('1 / 0') == 'ZeroDivisionError: integer division or modulo'
      and outcome('1 % 0') == 'ZeroDivisionError: integer division or modulo')

check('shift left', outcome('1 << 30') == '1073741824' and outcome('1 << 31') == '-2147483648'
      and outcome('1 << 32') == '0' and outcome('3 << 31') == '-2147483648' and outcome('5 << 0') == '5')
check('shift right', outcome('-1 >> 31') == '-1' and outcome('-1 >> 40') == '-1' and outcome('2147483647 >> 31') == '0'
      and outcome('(-2147483647 - 1) >> 1') == '-1073741824' and outcome('2147483647 >> 40') == '0')
check('negative shift', outcome('1 << -1') == 'ValueError: negative shift count'
      and outcome('1 >> -1') == 'ValueError: negative shift count')
check('bit operations', outcome('-1 & 255') == '255' and outcome('-1 ^ 1') == '-2' and outcome('0 | MIN') == '-2147483648'
      and outcome('~0') == '-1' and outcome('~2147483647') == '-2147483648')

check('hex of a negative number', outcome('hex(-1)') == "'0xffffffff'" and outcome('hex(MIN)') == "'0x80000000'"
      and outcome('hex(255)') == "'0xff'" and outcome('hex(0)') == "'0x0'")
check('oct of a negative number', outcome('oct(-1)') == "'037777777777'" and outcome('oct(MIN)') == "'020000000000'"
      and outcome('oct(0)') == "'0'" and outcome('oct(8)') == "'010'")
check('format x', outcome("'%x' % -1") == "'ffffffff'" and outcome("'%X' % -255") == "'FFFFFF01'"
      and outcome("'%#x' % -1") == "'0xffffffff'" and outcome("'%08x' % 255") == "'000000ff'")
check('format o', outcome("'%o' % -1") == "'37777777777'" and outcome("'%#o' % 8") == "'010'")
check('format d', outcome("'%d' % MIN") == "'-2147483648'" and outcome("'%d' % 2147483647") == "'2147483647'"
      and outcome("'%5d|' % -3") == "'   -3|'")
check('format of a long', outcome("'%d' % 2147483648L") == 'OverflowError: long int too long to convert')
check('format of a float', outcome("'%d' % 2147483648.0") == 'OverflowError: float too large to convert'
      and outcome("'%d' % 3.7") == "'3'")
check('repr', repr(2147483647) == '2147483647' and repr(MIN) == '-2147483648' and str(-5) == '-5')

check('literal 0xffffffff', outcome('0xffffffff') == '-1' and outcome('0xFFFFFFFF') == '-1')
check('literal 0x80000000', outcome('0x80000000') == '-2147483648' and outcome('0x7fffffff') == '2147483647')
check('octal literal', outcome('037777777777') == '-1' and outcome('020000000000') == '-2147483648')
check('literal too large', outcome('2147483648') == 'OverflowError: integer literal too large'
      and outcome('4294967296') == 'OverflowError: integer literal too large'
      and outcome('-2147483648') == 'OverflowError: integer literal too large'
      and outcome('0x100000000') == 'OverflowError: integer literal too large'
      and outcome('040000000000') == 'OverflowError: integer literal too large')
check('long literals', outcome('2147483648L') == '2147483648L' and outcome('-2147483648L') == '-2147483648L'
      and outcome('4294967296L') == '4294967296L')
check('0xffffffff equals minus one', 0xffffffff == -1 and 0xffffffffL == 4294967295L)

check('int of a string', outcome("int('2147483647')") == '2147483647' and outcome("int('-2147483648')") == '-2147483648'
      and outcome("int(' 12 ')") == '12')
check('int of a big string', outcome("int('2147483648')") == 'ValueError: int() literal too large: 2147483648'
      and outcome("int('-2147483649')") == 'ValueError: int() literal too large: -2147483649')
check('int of a bad string', outcome("int('12a')") == 'ValueError: invalid literal for int(): 12a'
      and outcome("int('5\\xa0')") == 'ValueError: invalid literal for int(): 5\xa0')
check('long of a string', outcome("long('2147483648')") == '2147483648L')
check('int of a float', outcome('int(2147483647.9)') == '2147483647' and outcome('int(-2147483648.5)') == '-2147483648'
      and outcome('int(-3.9)') == '-3')
check('int of a big float', outcome('int(2147483648.0)') == 'OverflowError: float too large to convert'
      and outcome('int(-2147483649.0)') == 'OverflowError: float too large to convert')
qnan = struct.unpack('d', '\x00\x00\x00\x00\x00\x00\xf8\x7f')[0]
check('int of a NaN', outcome('int(qnan)') == 'OverflowError: float too large to convert')
check('int of a long', outcome('int(2147483647L)') == '2147483647' and outcome('int(-2147483648L)') == '-2147483648'
      and outcome('int(2147483648L)') == 'OverflowError: long int too long to convert'
      and outcome('int(-2147483649L)') == 'OverflowError: long int too long to convert')
check('long of an int', repr(long(-5)) == '-5L' and repr(long(MIN)) == '-2147483648L')
check('long arithmetic', repr(2147483647L + 1) == '2147483648L' and repr(2L ** 40) == '1099511627776L'
      and repr(-2147483648L - 1) == '-2147483649L')
check('float of an int', float(MIN) == -2147483648.0 and float(2147483647) == 2147483647.0)
check('range', range(3) == [0, 1, 2] and range(5, 0, -2) == [5, 3, 1] and range(-2, 2) == [-2, -1, 0, 1])
check('xrange', len(xrange(10)) == 10 and xrange(3, 8)[2] == 5 and len(xrange(0, 100, 7)) == 15)
check('big range arguments', outcome('range(2147483648L)') == 'OverflowError: long int too long to convert')

check('struct sizes', struct.calcsize('l') == 4 and struct.calcsize('i') == 4 and struct.calcsize('h') == 2
      and struct.calcsize('ll') == 8 and struct.calcsize('cl') == 8 and struct.calcsize('chl') == 8
      and struct.calcsize('lc') == 5 and struct.calcsize('<l') == 4)
check('struct pack l', struct.pack('<l', -1) == '\xff\xff\xff\xff' and struct.pack('<l', 2147483647) == '\xff\xff\xff\x7f'
      and struct.pack('<l', MIN) == '\x00\x00\x00\x80' and struct.pack('l', 1) == '\x01\x00\x00\x00')
check('struct unpack l', struct.unpack('<l', '\x00\x00\x00\x80') == (MIN,)
      and struct.unpack('<l', '\xff\xff\xff\xff') == (-1,) and struct.unpack('l', '\x05\x00\x00\x00') == (5,))
check('struct pack of a long', struct.pack('<l', 2147483647L) == '\xff\xff\xff\x7f')
check('struct pack too large',
      outcome("struct.pack('<l', 2147483648L)") == 'OverflowError: long int too long to convert')

check('marshal int', marshal.dumps(1) == 'i\x01\x00\x00\x00' and marshal.dumps(-1) == 'i\xff\xff\xff\xff'
      and marshal.dumps(2147483647) == 'i\xff\xff\xff\x7f' and marshal.dumps(MIN) == 'i\x00\x00\x00\x80')
check('marshal reads a 64-bit int', marshal.loads('I\x00\x00\x00\x80\x00\x00\x00\x00') == MIN
      and marshal.loads('I\xff\xff\xff\xff\x00\x00\x00\x00') == -1 and marshal.loads('I\x07\x00\x00\x00\x00\x00\x00\x00') == 7)
check('marshal round trip', marshal.loads(marshal.dumps((1, -1, 2147483647, MIN, 5L, 2L ** 40, -2L ** 40, 1.5, 'x')))
      == (1, -1, 2147483647, MIN, 5L, 2L ** 40, -2L ** 40, 1.5, 'x'))
check('marshal long', marshal.dumps(2147483648L) == 'l\x03\x00\x00\x00\x00\x00\x00\x00\x02\x00')

values = array.array('l', [1, -1, 2147483647, MIN])
check('array l', values.itemsize == 4 and values.tolist() == [1, -1, 2147483647, MIN]
      and len(values.tostring()) == 16 and values.tostring()[4:8] == '\xff\xff\xff\xff')

# BEGIN HASHES
# (object, hash value of the exe): the objects are ('s', text), ('rep', text, count), ('i', value), ('f', text of a float),
# ('l', text of an integer), ('t', (objects)). The values come from the functions of the exe (py_long.expectations_match_exe).
HASHES = [
    (('s', ''), 0),
    (('s', 'a'), -468864544),
    (('s', 'abc'), -1600925533),
    (('s', 'Hello, World'), 999199932),
    (('s', 'Position'), 333370969),
    (('s', '\xff\x80\x01'), -1840802187),
    (('rep', 'x', 300), -1822343732),
    (('i', 5), 5),
    (('i', -1), -2),
    (('i', 2147483647), 2147483647),
    (('i', MIN), MIN),
    (('f', '1.5'), 1073741825),
    (('f', '0.1'), -1718085018),
    (('f', '-2.25'), -1073774594),
    (('f', '2147483648.5'), -1073741824),
    (('f', '10000000000.0'), 1410065410),
    (('f', '3.0'), 3),
    (('f', '1e100'), 707110669),
    (('f', '0.5'), 1073741824),
    (('l', '1099511627776'), 256),
    (('l', '-1099511627776'), -256),
    (('l', '2147483648'), MIN),
    (('l', '100000000000000000000'), -824746446),
    (('t', (('i', 1), ('i', 2))), 219750523),
    (('t', (('s', 'a'), ('i', -1), ('f', '1.5'))), 1452583600),
    (('t', ()), 3430008),
]
# END HASHES

# BEGIN ORDERS
# (operations, keys in the order of keys()): an operation is ('S', key) for an assignment and ('D', key) for a delete.
# The orders come from the dictionary code of the exe (py_long.expectations_match_exe).
ORDERS = [
    ([('S', ('s', 'Ship')), ('S', ('s', 'Type')), ('S', ('s', 'Position')), ('S', ('s', 'Hull')), ('S', ('s', 'Shield')),
      ('S', ('s', 'Phaser')), ('S', ('s', 'Torpedo')), ('S', ('s', 'Warp')), ('S', ('s', 'Sensor')), ('S', ('s', 'Power')),
      ('S', ('s', 'Cloak')), ('S', ('s', 'Repair')), ('S', ('s', 'Tractor')), ('S', ('s', 'Impulse')), ('S', ('s', 'Crew')),
      ('S', ('s', 'Name')), ('S', ('s', 'Mission')), ('S', ('s', 'Episode')), ('S', ('s', 'Species')), ('S', ('s', 'Race'))],
     [('s', 'Tractor'), ('s', 'Mission'), ('s', 'Sensor'), ('s', 'Ship'), ('s', 'Position'), ('s', 'Impulse'),
      ('s', 'Repair'), ('s', 'Race'), ('s', 'Torpedo'), ('s', 'Crew'), ('s', 'Warp'), ('s', 'Type'), ('s', 'Species'),
      ('s', 'Hull'), ('s', 'Name'), ('s', 'Power'), ('s', 'Shield'), ('s', 'Episode'), ('s', 'Phaser'), ('s', 'Cloak')]),
    ([('S', ('i', 5)), ('S', ('i', 100)), ('S', ('i', -1)), ('S', ('i', 0)), ('S', ('i', 2147483647)),
      ('S', ('i', MIN)), ('S', ('i', 65536)), ('S', ('i', 31)), ('S', ('i', 1000)), ('S', ('i', 7)),
      ('S', ('i', -100)), ('S', ('i', 12345)), ('S', ('i', 3)), ('S', ('i', 4)), ('S', ('i', 2))],
     [('i', 2147483647), ('i', -1), ('i', 3), ('i', -100), ('i', 12345), ('i', 4), ('i', MIN), ('i', 1000),
      ('i', 7), ('i', 5), ('i', 100), ('i', 31), ('i', 2), ('i', 0), ('i', 65536)]),
    ([('S', ('s', 'a')), ('S', ('i', 1)), ('S', ('s', 'b')), ('S', ('i', -2)), ('S', ('s', 'Hello')), ('S', ('i', 70000)),
      ('S', ('rep', 'x', 20)), ('S', ('i', 2147483647))],
     [('i', 2147483647), ('i', -2), ('s', 'Hello'), ('s', 'a'), ('s', 'b'), ('rep', 'x', 20), ('i', 1), ('i', 70000)]),
    ([('S', ('i', -1)), ('S', ('i', -2)), ('S', ('i', -3)), ('S', ('i', -5)), ('S', ('i', -6)), ('S', ('i', -7)),
      ('S', ('i', -8)), ('S', ('i', -9)), ('S', ('i', -10)), ('S', ('i', -11)), ('S', ('i', -12)), ('S', ('i', -13)),
      ('S', ('i', -100)), ('S', ('i', -101)), ('S', ('i', -1000))],
     [('i', -1), ('i', -3), ('i', -2), ('i', -5), ('i', -6), ('i', -7), ('i', -8), ('i', -9), ('i', -10), ('i', -11),
      ('i', -12), ('i', -13), ('i', -100), ('i', -101), ('i', -1000)]),
]
# END ORDERS


def build(spec):
    kind = spec[0]
    if kind == 's' or kind == 'i':
        return spec[1]
    if kind == 'rep':
        return spec[1] * spec[2]
    if kind == 'f':
        return float(spec[1])
    if kind == 'l':
        return long(spec[1])
    items = []
    for item in spec[1]:
        items.append(build(item))
    return tuple(items)


bad = []
for spec, expected in HASHES:
    if hash(build(spec)) != expected:
        bad.append(repr(spec))
check('hash values of the exe', len(bad) == 0)

bad = []
for ops, expected in ORDERS:
    table = {}
    for op, key in ops:
        if op == 'S':
            table[build(key)] = 1
        else:
            del table[build(key)]
    wanted = []
    for key in expected:
        wanted.append(build(key))
    if table.keys() != wanted:
        bad.append(repr(table.keys()))
check('dictionary key order of the exe', len(bad) == 0)

lst = {}
lst['b'] = 1
lst['a'] = 2
check('keys, values and items have one order', len(lst.keys()) == 2 and lst.items() == [(lst.keys()[0], lst.values()[0]),
                                                                                        (lst.keys()[1], lst.values()[1])])


class Plain:
    pass


one, two = Plain(), Plain()
check('id is an int of 32 bits', type(id(one)) == type(0) and id(one) == id(one) and id(one) != id(two))
check('hash of an instance is its id', hash(one) == id(one) or (id(one) == -1 and hash(one) == -2))
check('hash of None and of a function',
      type(hash(None)) == type(0) and type(hash(check)) == type(0) and hash(check) == hash(check))
