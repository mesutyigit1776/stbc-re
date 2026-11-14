#include "TGModelPropertyList.h"

// 0x6edff0
TGModelPropertyList::TGModelPropertyList() : unused(0) { items.Reset(); }

// 0x6ee040, called by the deleting destructor 0x6ee020
TGModelPropertyList::~TGModelPropertyList() { items.ReleaseBlocks(); }
