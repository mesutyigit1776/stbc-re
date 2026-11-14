#include "securelevel.h"

/* 0x99ee38 */
static int g_securelevel;

/* 0x74bbf0, E0108 */
void Py_SetSecurelevel(int raise)
{
	if (raise)
		g_securelevel++;
	else
		g_securelevel--;
}

/* 0x74bc10, E0108 */
int Py_IsSecurelevelOn(void)
{
	return g_securelevel > 0;
}
