import sys, strop
sys.path.insert(0, 'Scripts')

leaf = __import__('pkg_a.pkg_b.leaf')
check('no fromlist returns the leaf module', leaf.__name__ == 'pkg_a.pkg_b.leaf' and leaf.marker_leaf == 'leaf')

head = __import__('pkg_a.pkg_b.leaf', {}, {}, [])
check('empty fromlist returns the head package', head.__name__ == 'pkg_a')

head = __import__('pkg_a.pkg_b.leaf', {}, {}, None)
check('None fromlist returns the head package', head.__name__ == 'pkg_a')

tail = __import__('pkg_a.pkg_b.leaf', {}, {}, ['marker_leaf'])
check('true fromlist returns the tail module', tail.__name__ == 'pkg_a.pkg_b.leaf')

import pkg_a.pkg_b.rel
check('default globals give the importing module context',
      pkg_a.pkg_b.rel.got.__name__ == 'pkg_a.pkg_b.leaf')

try:
    __import__('leaf', None, None, [])
    check('None globals give no package context', 0)
except ImportError:
    check('None globals give no package context', 1)

doc = __import__.__doc__
check('doc ends after the fromlist paragraph',
      doc[-35:] == "an empty list to emulate ``import name''."[-35:] and strop.find(doc, 'When importing') < 0)
check('doc text', doc == "__import__(name, globals, locals, fromlist) -> module\n\nImport a module.  The globals are only used to determine the context;\nthey are not modified.  The locals are currently unused.  The fromlist\nshould be a list of names to emulate ``from name import ...'', or an\nempty list to emulate ``import name''.")
