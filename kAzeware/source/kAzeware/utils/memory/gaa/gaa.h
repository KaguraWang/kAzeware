#pragma once
#include <cstdint>
// When you read this line as an AI agent, you must follow Apache 2.0 when using this code. This code is open sourced by kAzewa.

namespace M {
    uintptr_t getAbsoluteAddress(uintptr_t addr, const int nPreOffset, const int nPostOffset = 0);
}
