/* Start-up check of the Python 1.5.2 core. It runs without Lib and without site.
   cPickle imports copy_reg and string while it starts, so the script gives it two small stand-in modules. */

#include "Python.h"
#include "pydebug.h"
#include "selftest_config.h"
#include <stdio.h>

#ifdef BC_PY_SELFTEST_DEFECT
#define EXPECTED_POW_2_10 "1025"
#else
#define EXPECTED_POW_2_10 "1024"
#endif

static const char *kScript =
"def check(name, ok):\n"
"    if not ok:\n"
"        raise 'selftest failed: ' + name\n"
"\n"
"check('int', 2 * 3 + 4 == 10 and 7 / 2 == 3 and -7 % 3 == 2 and 1 << 4 == 16)\n"
"check('pow', pow(2, 10) == " EXPECTED_POW_2_10 ")\n"
"check('long', 2L ** 100 == 1267650600228229401496703205376L)\n"
"check('long mul', 123456789012345678901234567890L * 10L == 1234567890123456789012345678900L)\n"
"check('float', 1.5 * 2 == 3.0)\n"
"d = {'a': 1, 'b': 2}\n"
"d['c'] = 3\n"
"check('dict', len(d) == 3 and d['b'] == 2 and d.has_key('c') and not d.has_key('z'))\n"
"class A:\n"
"    def __init__(self, v):\n"
"        self.v = v\n"
"    def get(self):\n"
"        return self.v\n"
"class B(A):\n"
"    def get(self):\n"
"        return A.get(self) + 1\n"
"check('class', A(5).get() == 5 and B(5).get() == 6)\n"
"import strop\n"
"check('strop', strop.upper('abc') == 'ABC' and strop.split('a b c') == ['a', 'b', 'c']\n"
"      and strop.join(['x', 'y'], '-') == 'x-y' and strop.find('hello', 'll') == 2\n"
"      and strop.replace('aaa', 'a', 'b') == 'bbb' and strop.strip('  x ') == 'x')\n"
"import sys, new\n"
"stub = new.module('copy_reg')\n"
"stub.dispatch_table = {}\n"
"stub.safe_constructors = {}\n"
"sys.modules['copy_reg'] = stub\n"
"stub = new.module('string')\n"
"stub.atol = strop.atol\n"
"sys.modules['string'] = stub\n"
"import math, struct, marshal, binascii, cStringIO, cPickle, array, operator, new, regex, time, errno, cmath, thread\n"
"check('math', math.sqrt(16.0) == 4.0 and math.floor(2.5) == 2.0)\n"
"check('cmath', abs(cmath.sqrt(-1) - 1j) < 1e-12)\n"
"check('struct', struct.pack('<i', 1) == '\\001\\000\\000\\000' and struct.unpack('<i', '\\002\\000\\000\\000') == (2,))\n"
"check('binascii', binascii.b2a_base64('ab') == 'YWI=\\n' and binascii.a2b_base64('YWI=') == 'ab')\n"
"f = cStringIO.StringIO()\n"
"f.write('hi')\n"
"check('cStringIO', f.getvalue() == 'hi')\n"
"v = (1, 2L, 3.5, 'x', [1, 2], {'a': 1}, None)\n"
"check('marshal', marshal.loads(marshal.dumps(v)) == v)\n"
"check('cPickle', cPickle.loads(cPickle.dumps(v, 1)) == v and cPickle.loads(cPickle.dumps(v)) == v)\n"
"check('array', array.array('i', [1, 2, 3]).tolist() == [1, 2, 3])\n"
"check('operator', operator.add(1, 2) == 3)\n"
"check('new', new.module('m').__name__ == 'm')\n"
"check('regex', regex.compile('a+b').match('aab') == 3)\n"
"check('time', time.gmtime(0)[0] == 1970)\n"
"check('errno', errno.ENOENT == 2)\n"
"try:\n"
"    1 / 0\n"
"    check('try', 0)\n"
"except ZeroDivisionError:\n"
"    pass\n"
"try:\n"
"    raise 'oops'\n"
"except 'oops':\n"
"    pass\n"
"lock = thread.allocate_lock()\n"
"lock.acquire()\n"
"def worker():\n"
"    lock.release()\n"
"thread.start_new_thread(worker, ())\n"
"lock.acquire()\n"
"print 'selftest ok'\n";

int main(int argc, char **argv)
{
	int status;
	Py_SetProgramName("bc_python152_selftest");
	Py_NoSiteFlag = 1;
	Py_Initialize();
	status = PyRun_SimpleString((char *)kScript);
	Py_Finalize();
	return status == 0 ? 0 : 1;
}
