#include "prx/libSceAgc/Misc/include/SemaphoreMemory.hpp"

#include <cstddef>
#include <cstring>
#include <mutex>

namespace {

constexpr int32_t invalidAlignment = static_cast<int32_t>(0x8a6c0002u);
constexpr int32_t invalidValue = static_cast<int32_t>(0x8a6c000bu);
constexpr int32_t alreadyInitialized = static_cast<int32_t>(0x8a6c0048u);
constexpr int32_t notInitialized = static_cast<int32_t>(0x8a6c0049u);
std::mutex semaphoreMutex;
std::byte* semaphoreMemory = nullptr;
uint64_t semaphoreSize = 0;

}

extern "C" {

int32_t APS5_VABI sceAgcSetAmmSemaphoreMemory(void* memory, uint64_t sizeInBytes) {
    const std::lock_guard lock(semaphoreMutex);
    if (semaphoreSize != 0) return alreadyInitialized;
    if (memory == nullptr || sizeInBytes == 0
        || ((reinterpret_cast<uintptr_t>(memory) | sizeInBytes) & 0x3fffu) != 0) {
        return invalidAlignment;
    }
    std::memset(memory, 0, static_cast<size_t>(sizeInBytes));
    semaphoreMemory = static_cast<std::byte*>(memory);
    semaphoreSize = sizeInBytes;
    return 0;
}

int32_t APS5_VABI sceAgcGetSemaphoreLabel(uint32_t index, void** labelOut) {
    const std::lock_guard lock(semaphoreMutex);
    if (semaphoreSize == 0) return notInitialized;
    const uint64_t end = (static_cast<uint64_t>(index) + 1) * 32;
    if (end > semaphoreSize) return invalidValue;
    if (labelOut == nullptr) return invalidAlignment;
    *labelOut = semaphoreMemory + static_cast<uint64_t>(index) * 32;
    return 0;
}

}
