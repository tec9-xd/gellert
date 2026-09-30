#include "memory.hpp"
#include "log.hpp"
#include <dlfcn.h>
#include <link.h>
#include <cstring>
#include <unistd.h>
#include <sys/mman.h>

namespace mem {
uintptr_t client = 0;

uintptr_t module_base(const char* needle) {
    struct Ctx { const char* n; uintptr_t* o; };
    uintptr_t base = 0;
    Ctx ctx{needle, &base};
    dl_iterate_phdr([](dl_phdr_info* i, size_t, void* v) -> int {
        auto* c = (Ctx*)v;
        if (i->dlpi_name && strstr(i->dlpi_name, c->n)) {
            *c->o = (uintptr_t)i->dlpi_addr;
            return 1;
        }
        return 0;
    }, &ctx);
    return base;
}

void* create_interface(const char* lib, const char* version) {
    void* h = dlopen(lib, RTLD_NOLOAD | RTLD_NOW);
    if (!h) { print("dlopen %s failed\n", lib); return nullptr; }
    auto fn = (void* (*)(const char*, int*))dlsym(h, "CreateInterface");
    dlclose(h);
    if (!fn) { print("CreateInterface missing in %s\n", lib); return nullptr; }
    void* iface = fn(version, nullptr);
    print("%s %s -> %p\n", lib, version, iface);
    return iface;
}

bool vmt_swap(void** table, int index, void* fn, void** out_orig) {
    if (!valid_ptr(table) || !valid_ptr(table[index])) return false;
    const long ps = sysconf(_SC_PAGESIZE);
    void* page = (void*)((uintptr_t)&table[index] & ~(ps - 1));
    if (mprotect(page, ps, PROT_READ | PROT_WRITE) != 0) return false;
    if (out_orig) *out_orig = table[index];
    table[index] = fn;
    mprotect(page, ps, PROT_READ);
    return true;
}
}
