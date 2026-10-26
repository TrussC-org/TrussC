#pragma once
#include <cstddef>
namespace allocation_test {
extern thread_local bool measure;
extern thread_local bool fail;
extern thread_local std::size_t allocations;
}
