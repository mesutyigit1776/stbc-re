#include "Trap.h"

#include <cstdio>
#include <cstdlib>

namespace bc {

static void AbortWithAddress(std::uint32_t exeAddress)
{
    std::fprintf(stderr, "not reconstructed: exe code at 0x%08x\n", exeAddress);
    std::abort();
}

static TrapHandler g_handler = AbortWithAddress;

void NotReconstructed(std::uint32_t exeAddress) { g_handler(exeAddress); }

TrapHandler SetTrapHandler(TrapHandler handler)
{
    TrapHandler previous = g_handler;
    g_handler = handler ? handler : AbortWithAddress;
    return previous;
}

}  // namespace bc
