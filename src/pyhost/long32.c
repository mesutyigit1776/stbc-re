/* The integer divisions of the interpreter that trap as the x86 instruction idiv does (B0019). */

#include "Python.h"
#include <errno.h>
#include <signal.h>

static void divide_trap(void)
{
	raise(SIGFPE);
}

void (*Py_IntegerDivideHook)(void) = divide_trap;

/* 0x74ca90 (i_divmod) and 0x74cb70 (int_pow). */
Py_long32 Py_IDiv(Py_long32 a, Py_long32 b)
{
	if (b == -1 && a == INT_MIN) {
		Py_IntegerDivideHook();
		return 0;
	}
	return a / b;
}

Py_long32 Py_IMod(Py_long32 a, Py_long32 b)
{
	if (b == -1 && a == INT_MIN) {
		Py_IntegerDivideHook();
		return 0;
	}
	return a % b;
}

/* 0x859d20 (_ftol). */
Py_long32 Py_DoubleToLong(double x)
{
	if (!(x >= -9223372036854775808.0 && x < 9223372036854775808.0))
		return 0;
	return (Py_long32)(Py_ulong32)(unsigned long long)(long long)x;
}

/* 0x85c337 (strtol of the C run time). The exe reads the 32-bit number of the pickle opcode INT with it. */
Py_long32 Py_CrtStrtol(const char *text, char **end, int base)
{
	const unsigned char *p = (const unsigned char *)text;
	Py_ulong32 number = 0, limit;
	int negative = 0, overflow = 0, read_digit = 0, c;

	if (base < 0 || base == 1 || base > 36) {
		if (end != NULL)
			*end = (char *)text;
		return 0;
	}
	while (Py_isspace(*p))
		p++;
	c = *p++;
	if (c == '-') {
		negative = 1;
		c = *p++;
	}
	else if (c == '+')
		c = *p++;
	if (base == 0) {
		if (c != '0')
			base = 10;
		else if (*p == 'x' || *p == 'X')
			base = 16;
		else
			base = 8;
	}
	if (base == 16 && c == '0' && (*p == 'x' || *p == 'X')) {
		p++;
		c = *p++;
	}
	limit = ULONG_MAX / base;
	for (;;) {
		int digit;
		if (Py_isdigit(c))
			digit = c - '0';
		else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
			digit = (c >= 'a' ? c - 'a' : c - 'A') + 10;
		else
			break;
		if (digit >= base)
			break;
		read_digit = 1;
		if (number < limit || (number == limit && (Py_ulong32)digit <= ULONG_MAX % base))
			number = number * base + digit;
		else
			overflow = 1;
		c = *p++;
	}
	p--;
	if (!read_digit) {
		p = (const unsigned char *)text;
		number = 0;
	}
	else if (overflow || (negative ? number > (Py_ulong32)LONG_MAX + 1 : number > (Py_ulong32)LONG_MAX)) {
		errno = ERANGE;
		number = negative ? (Py_ulong32)LONG_MIN : (Py_ulong32)LONG_MAX;
	}
	if (end != NULL)
		*end = (char *)p;
	return negative ? (Py_long32)(0 - number) : (Py_long32)number;
}
