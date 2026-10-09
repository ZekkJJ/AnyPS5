#include "prx/libSceAgc/Misc/include/SemaphoreMemory.hpp"
#include "prx/libc/include/Shutdown.hpp"

#include <algorithm>
#include <array>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace {

constexpr size_t alignment = 0x4000;
constexpr uint32_t labelStride = 32;
constexpr uint32_t invalidAlignment = 0x8a6c0002;
constexpr uint32_t invalidValue = 0x8a6c000b;
constexpr uint32_t alreadyInitialized = 0x8a6c0048;
constexpr uint32_t notInitialized = 0x8a6c0049;
constexpr std::byte untouched{0xa5};
std::span<std::byte> storage;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void checkError(int32_t result, uint32_t expected, const char* message) {
    check(static_cast<uint32_t>(result) == expected, message);
}

void checkBytes(size_t offset, size_t size, std::byte expected, const char* message) {
    check(std::all_of(storage.begin() + offset, storage.begin() + offset + size,
                     [expected](std::byte value) { return value == expected; }), message);
}

void testUninitialized() {
    std::array<void*, 3> labels{storage.data(), storage.data(), storage.data()};
    checkError(sceAgcGetSemaphoreLabel(0, &labels[1]), notInitialized, "uninitialized label lookup");
    check(labels[0] == storage.data() && labels[1] == storage.data() && labels[2] == storage.data(),
          "failed lookup modified output");
    checkError(sceAgcGetSemaphoreLabel(0, nullptr), notInitialized, "initialization precedes null output");
    checkError(sceAgcGetSemaphoreLabel(std::numeric_limits<uint32_t>::max(), nullptr), notInitialized,
               "initialization precedes index validation");
}

void testInvalidInitialization() {
    struct InvalidInput {
        void* memory;
        uint64_t size;
    };
    const std::array inputs{
        InvalidInput{nullptr, 0},
        InvalidInput{nullptr, alignment},
        InvalidInput{storage.data() + alignment, 0},
        InvalidInput{storage.data() + alignment, 1},
        InvalidInput{storage.data() + alignment, alignment - 1},
        InvalidInput{storage.data() + alignment, alignment + 1},
        InvalidInput{storage.data() + alignment + 1, alignment},
        InvalidInput{storage.data() + alignment + 8, alignment},
        InvalidInput{storage.data() + alignment + 1, 0},
    };
    for (const auto& input : inputs) {
        checkError(sceAgcSetAmmSemaphoreMemory(input.memory, input.size), invalidAlignment,
                   "invalid initialization did not return alignment error");
        testUninitialized();
    }
    checkBytes(0, storage.size(), untouched, "invalid initialization touched storage");
}

void testInitialized(size_t blockCount) {
    auto* memory = storage.data() + alignment;
    const size_t size = alignment * blockCount;
    check(sceAgcSetAmmSemaphoreMemory(memory, size) == 0, "aligned initialization failed");
    checkBytes(0, alignment, untouched, "initialization wrote before storage");
    checkBytes(alignment, size, std::byte{0}, "initialization did not clear all storage");
    checkBytes(alignment + size, storage.size() - alignment - size, untouched,
               "initialization wrote after storage");

    const uint32_t labelCount = static_cast<uint32_t>(size / labelStride);
    for (uint32_t index = 0; index < labelCount; ++index) {
        std::array<void*, 3> labels{storage.data(), nullptr, storage.data()};
        check(sceAgcGetSemaphoreLabel(index, &labels[1]) == 0, "valid label lookup failed");
        check(labels[1] == memory + index * labelStride, "incorrect label stride or base");
        check(labels[0] == storage.data() && labels[2] == storage.data(), "lookup overwrote output canary");
    }

    for (uint32_t index : {labelCount, labelCount + 1, std::numeric_limits<uint32_t>::max()}) {
        void* label = storage.data();
        checkError(sceAgcGetSemaphoreLabel(index, &label), invalidValue, "out-of-range index accepted");
        check(label == storage.data(), "failed lookup changed label");
        checkError(sceAgcGetSemaphoreLabel(index, nullptr), invalidValue, "bounds precede null output");
    }
    checkError(sceAgcGetSemaphoreLabel(0, nullptr), invalidAlignment, "null output accepted");

    std::fill(memory, memory + size, std::byte{0x3c});
    checkError(sceAgcSetAmmSemaphoreMemory(memory, size), alreadyInitialized, "duplicate initialization accepted");
    checkError(sceAgcSetAmmSemaphoreMemory(nullptr, 0), alreadyInitialized,
               "duplicate initialization must precede argument checks");
    checkError(sceAgcSetAmmSemaphoreMemory(storage.data(), alignment), alreadyInitialized,
               "replacement storage accepted");
    checkBytes(alignment, size, std::byte{0x3c}, "duplicate initialization erased labels");
    checkBytes(0, alignment, untouched, "duplicate initialization touched alternative storage");
    void* last = nullptr;
    check(sceAgcGetSemaphoreLabel(labelCount - 1, &last) == 0, "last label lookup failed");
    check(last == memory + size - labelStride, "last label does not fit in the registered region");
    checkBytes(alignment, size, std::byte{0x3c}, "lookup erased existing labels");
}

void testConcurrentInitialization() {
    constexpr size_t threadCount = 8;
    std::barrier start(static_cast<std::ptrdiff_t>(threadCount));
    std::array<int32_t, threadCount> results{};
    std::array<bool, threadCount> published{};
    std::array<std::jthread, threadCount> threads;
    for (size_t index = 0; index < threadCount; ++index) {
        threads[index] = std::jthread([&, index] {
            start.arrive_and_wait();
            results[index] = sceAgcSetAmmSemaphoreMemory(storage.data() + alignment, alignment * 3);
            void* label = nullptr;
            published[index] = sceAgcGetSemaphoreLabel(alignment * 3 / labelStride - 1, &label) == 0
                && label == storage.data() + alignment * 4 - labelStride
                && std::all_of(storage.begin() + alignment, storage.begin() + alignment * 4,
                               [](std::byte value) { return value == std::byte{0}; });
        });
    }
    for (auto& thread : threads) thread.join();
    check(std::count(results.begin(), results.end(), 0) == 1, "initialization did not have exactly one winner");
    for (size_t index = 0; index < threadCount; ++index) {
        if (results[index] != 0) checkError(results[index], alreadyInitialized, "concurrent loser returned wrong error");
        check(published[index], "initialized memory was not published with its size");
    }
    checkBytes(0, alignment, untouched, "concurrent initialization wrote before storage");
    checkBytes(alignment, alignment * 3, std::byte{0}, "concurrent initialization did not clear storage");
    checkBytes(alignment * 4, alignment, untouched, "concurrent initialization wrote after storage");
}

}

int main(int argc, char** argv) {
    try {
        check(argc == 2, "expected single, multiple or concurrent test case");
        auto release = [](std::byte* memory) { ::operator delete(memory, std::align_val_t{alignment}); };
        const std::unique_ptr<std::byte, decltype(release)> allocation(
            static_cast<std::byte*>(::operator new(alignment * 5, std::align_val_t{alignment})), release);
        storage = {allocation.get(), alignment * 5};
        std::fill(storage.begin(), storage.end(), untouched);
        testUninitialized();
        testInvalidInitialization();
        const std::string_view mode = argv[1];
        if (mode == "single") testInitialized(1);
        else if (mode == "multiple") testInitialized(3);
        else if (mode == "concurrent") testConcurrentInitialization();
        else throw std::runtime_error("unknown semaphore test case");
        LibcRunShutdown_nid_postfix();
        std::puts("AGC semaphore memory tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        try { LibcRunShutdown_nid_postfix(); }
        catch (const std::exception& shutdown) { std::fprintf(stderr, "shutdown: %s\n", shutdown.what()); }
        return 1;
    }
}
