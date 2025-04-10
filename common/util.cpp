#include "util.hpp"

#include <new>

void Abort_AllocFailed()
{
    Log(LogLevel::SEVERE, "Failed to allocate memory");
    throw std::bad_alloc {};
}
