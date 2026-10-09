#ifndef CORE_LIBS_PRX_LIBSCEAGC_MISC_INCLUDE_SEMAPHOREMEMORY_HPP
#define CORE_LIBS_PRX_LIBSCEAGC_MISC_INCLUDE_SEMAPHOREMEMORY_HPP

#include "prx/libc/include/general/VabiMacros.hpp"

#include <cstdint>

extern "C" {

int32_t APS5_VABI sceAgcSetAmmSemaphoreMemory(void* memory, uint64_t sizeInBytes);
int32_t APS5_VABI sceAgcGetSemaphoreLabel(uint32_t index, void** labelOut);

}

#endif
