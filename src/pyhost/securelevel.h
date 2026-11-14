/* The securelevel counter of the interpreter in stbc.exe (E0108). */

#ifndef BC_PYHOST_SECURELEVEL_H
#define BC_PYHOST_SECURELEVEL_H

/* 0x74bbf0: a non-zero argument raises the counter, zero lowers it. */
extern void Py_SetSecurelevel(int raise);

/* 0x74bc10: returns 1 when the counter is above 0. */
extern int Py_IsSecurelevelOn(void);

#endif
