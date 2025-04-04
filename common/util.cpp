#include "util.hpp"

#include <stdexcept>

void Abort_AllocFailed()
{
    Log(LogLevel::SEVERE, "Failed to allocate memory");
    throw std::runtime_error("Failed to allocate memory");
}
