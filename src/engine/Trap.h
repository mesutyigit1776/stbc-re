#pragma once
#include <cstdint>

namespace bc {

using TrapHandler = void (*)(std::uint32_t exeAddress);

// Marks code that the exe has and the source tree does not have yet. The default handler prints the exe
// address and aborts. A test can install a handler that returns.
void NotReconstructed(std::uint32_t exeAddress);
TrapHandler SetTrapHandler(TrapHandler handler);

}  // namespace bc

#define BC_NOT_RECONSTRUCTED(address) ::bc::NotReconstructed(address)
