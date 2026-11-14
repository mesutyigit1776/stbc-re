#include "ClassFactoryTable.h"

#include <cstdlib>
#include <new>

alignas(ClassFactoryTable) static unsigned char g_storage[sizeof(ClassFactoryTable)];
std::uint8_t ClassFactoryTable::destroyGuard = 0;

ClassFactoryTable* ClassFactoryTable::Global() { return reinterpret_cast<ClassFactoryTable*>(g_storage); }

// 0x6f08b0
void ClassFactoryTable::InitGlobal()
{
    new (g_storage) ClassFactoryTable();
}

// 0x6f08f0
void ClassFactoryTable::RegisterDestroyAtExit() { std::atexit(&ClassFactoryTable::DestroyGlobal); }

// 0x6f0900, registered with atexit at start-up; the bit with the value 4 in the byte at 0x99a524 makes it run once
void ClassFactoryTable::DestroyGlobal()
{
    if (destroyGuard & 4)
        return;
    destroyGuard |= 4;
    Global()->~ClassFactoryTable();
}
