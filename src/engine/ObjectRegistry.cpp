#include "ObjectRegistry.h"

#include "ClassFactoryTable.h"

// Both tables are built from the one template. Their code lives here once.
template class IdHashTable<TGObject*>;
template class IdHashTable<Creator>;
