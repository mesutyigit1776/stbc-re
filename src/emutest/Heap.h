#pragma once
#include <cstdint>

void HeapDiscount(std::int64_t allocations, std::int64_t frees);
std::int64_t HeapAllocations();
std::int64_t HeapFrees();
