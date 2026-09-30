#pragma once
#include <cstdint>
#include <cstddef>

inline bool valid_ptr(const void* p) {
    auto a = (uintptr_t)p;
    return a > 0x10000ull && a < 0x00007FFFFFFFFFFFull;
}

namespace mem {
    extern uintptr_t client;
    uintptr_t module_base(const char* needle);
    void*     create_interface(const char* lib, const char* version);
    bool      vmt_swap(void** table, int index, void* fn, void** out_orig);
}
