#include "util.hpp"

[[noreturn]] void Abort_AllocFailed()
{
    Log(LogLevel::SEVERE, "Failed to allocate memory, aborting");
    std::abort();
}
