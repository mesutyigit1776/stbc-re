import _locale

names = ['setlocale', 'localeconv', 'strcoll', 'strxfrm']
check('the four functions exist', len(filter(lambda n: hasattr(_locale, n), names)) == 4)
values = [('LC_CTYPE', 2), ('LC_TIME', 5), ('LC_COLLATE', 1), ('LC_MONETARY', 3), ('LC_NUMERIC', 4), ('LC_ALL', 0), ('CHAR_MAX', 127)]
bad = []
for name, value in values:
    if not hasattr(_locale, name) or getattr(_locale, name) != value:
        bad.append(name)
check('constants have the Windows values ' + repr(bad), bad == [])
check('no LC_MESSAGES', not hasattr(_locale, 'LC_MESSAGES'))
check('module name and doc', _locale.__name__ == '_locale' and _locale.__doc__ == 'Support for POSIX locales.')
check('Error', _locale.Error == 'locale.Error')
check('doc of setlocale', _locale.setlocale.__doc__ == '(integer,string=None) -> string. Activates/queries locale processing.')
check('doc of localeconv', _locale.localeconv.__doc__ == '() -> dict. Returns numeric and monetary locale-specific parameters.')
check('doc of strcoll', _locale.strcoll.__doc__ == 'string,string -> int. Compares two strings according to the locale.')
check('doc of strxfrm', _locale.strxfrm.__doc__ == 'string -> string. Returns a string that behaves for cmp locale-aware.')
check('setlocale query gives the C locale', _locale.setlocale(_locale.LC_ALL) == 'C')
check('strcoll', _locale.strcoll('a', 'b') < 0 and _locale.strcoll('b', 'a') > 0 and _locale.strcoll('a', 'a') == 0)
check('strxfrm in the C locale', _locale.strxfrm('abc') == 'abc')
check('localeconv decimal point', _locale.localeconv()['decimal_point'] == '.')
