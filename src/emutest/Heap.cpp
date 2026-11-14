// The allocator of the test library. The engine allocates through operator new, so the tests can compare the
// blocks that native code allocates and frees with the blocks of the original. The memory is zeroed, like the
// scratch heap of the emulator, and is never returned: a test may read an object after it was destroyed.
// Every block has guard bytes before and behind it. HeapOverruns lists the blocks whose guard bytes changed.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

constexpr std::size_t kGuard = 16;
constexpr unsigned char kGuardFill = 0xFD;

// The front guard is padded to 32 bytes so that the user pointer keeps the 16 byte alignment of calloc.
constexpr std::size_t kPrefix = 2 * kGuard;

struct Record {
    unsigned char* user;
    std::size_t size;
    bool reported;
};

std::int64_t g_allocations = 0;
std::int64_t g_frees = 0;
Record* g_records = nullptr;
std::size_t g_recordCount = 0;
std::size_t g_recordCapacity = 0;
std::size_t g_mark = 0;

void AddRecord(unsigned char* user, std::size_t size)
{
    if (g_recordCount == g_recordCapacity) {
        g_recordCapacity = g_recordCapacity ? g_recordCapacity * 2 : 4096;
        g_records = static_cast<Record*>(std::realloc(g_records, g_recordCapacity * sizeof(Record)));
        if (g_records == nullptr)
            std::abort();
    }
    g_records[g_recordCount++] = {user, size, false};
}

// Returns the offset of the first changed guard byte (negative before the block), or 0 when the guards are intact.
long GuardProblem(const Record& record)
{
    const unsigned char* before = record.user - kGuard;
    const unsigned char* after = record.user + record.size;
    for (std::size_t i = 0; i < kGuard; ++i) {
        if (before[i] != kGuardFill)
            return static_cast<long>(i) - static_cast<long>(kGuard);
    }
    for (std::size_t i = 0; i < kGuard; ++i) {
        if (after[i] != kGuardFill)
            return static_cast<long>(record.size + i);
    }
    return 0;
}

}  // namespace

void* operator new(std::size_t size)
{
    unsigned char* raw = static_cast<unsigned char*>(std::calloc(kPrefix + size + kGuard, 1));
    if (raw == nullptr)
        throw std::bad_alloc();
    unsigned char* user = raw + kPrefix;
    std::memset(user - kGuard, kGuardFill, kGuard);
    std::memset(user + size, kGuardFill, kGuard);
    AddRecord(user, size);
    ++g_allocations;
    return user;
}

void operator delete(void* block) noexcept
{
    if (block != nullptr)
        ++g_frees;
}

void operator delete(void* block, std::size_t) noexcept { operator delete(block); }

// Removes blocks that a test helper allocated and freed from the counts, so that the counts show the tested code only.
void HeapDiscount(std::int64_t allocations, std::int64_t frees)
{
    g_allocations -= allocations;
    g_frees -= frees;
}

std::int64_t HeapAllocations() { return g_allocations; }
std::int64_t HeapFrees() { return g_frees; }

extern "C" {
std::int64_t bc_heap_allocations() { return g_allocations; }
std::int64_t bc_heap_frees() { return g_frees; }

// Only blocks allocated after the mark are checked, so that the cost of a check does not grow with the run.
void bc_heap_mark() { g_mark = g_recordCount; }

// Writes one line "address size offset" for each block (after the mark) whose guard bytes changed and that
// was not reported before. Returns the number of blocks, or -1 when the text does not fit.
int bc_heap_overruns(char* out, int capacity)
{
    int found = 0, used = 0;
    for (std::size_t i = g_mark; i < g_recordCount; ++i) {
        Record& record = g_records[i];
        if (record.reported)
            continue;
        long offset = GuardProblem(record);
        if (offset == 0)
            continue;
        record.reported = true;
        char line[96];
        int length = std::snprintf(line, sizeof(line), "%llu %zu %ld\n",
                                   static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(record.user)),
                                   record.size, offset);
        if (used + length + 1 > capacity)
            return -1;
        std::memcpy(out + used, line, static_cast<std::size_t>(length));
        used += length;
        ++found;
    }
    out[used] = 0;
    return found;
}
}
